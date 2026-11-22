#include "qmlcache.hpp"
#include <array>
#include <cstring>
#include <mutex>
#include <new>
#include <span>
#include <utility>
#include <vector>

#include <private/qv4compileddata_p.h>
#include <qbytearray.h>
#include <qcryptographichash.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qelapsedtimer.h>
#include <qendian.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmutex.h>
#include <qqmlprivate.h>
#include <qset.h>
#include <qstring.h>
#include <qtenvironmentvariables.h>
#include <qurl.h>

#include "logcat.hpp"

namespace qs::qmlcache {

namespace {

QS_LOGGING_CATEGORY(logQmlCache, "quickshell.qmlcache", QtInfoMsg);

constexpr std::array<char, 8> BUNDLE_MAGIC = {'q', 's', 'q', 'm', 'l', 'c', 'b', '1'};
constexpr quint32 BUNDLE_FORMAT = 1;
constexpr qint64 BUNDLE_MAX_SIZE = qint64(1) << 30;
constexpr std::align_val_t BUNDLE_ALIGNMENT {16};

struct BundleHeader {
	std::array<char, 8> magic;
	quint32_le format;
	quint32_le count;
	std::array<char, 16> qtVersion;
	quint32_le stringsOffset;
	quint32_le stringsSize;
	std::array<quint32_le, 2> reserved;
};

static_assert(sizeof(BundleHeader) == 48);

struct BundleEntry {
	quint32_le pathOffset;
	quint32_le pathSize;
	quint32_le unitOffset;
	quint32_le unitSize;
	std::array<char, 16> sourceMd5;
};

static_assert(sizeof(BundleEntry) == 32);

const std::array<QQmlPrivate::AOTCompiledFunction, 1> NO_AOT_FUNCTIONS = {
    {{0, 0, nullptr, nullptr}}
};

struct Unit {
	QQmlPrivate::CachedQmlUnit cached {};
	QByteArray sourceMd5;
};

struct Bundle {
	std::vector<Unit> units;
	QHash<QString, size_t> index;
};

struct BundleKey {
	QString path;
	qint64 size = -1;
	QDateTime modified;

	bool operator==(const BundleKey& other) const = default;
};

struct State {
	QMutex mutex;
	QDir root;
	BundleKey key;
	Bundle* bundle = nullptr;
	QHash<QString, QByteArray> hashes;
	QSet<QString> intercepts;
};

State& globalState() {
	static auto* state = new State();
	return *state;
}

QByteArray hashFile(const QString& path) {
	auto file = QFile(path);
	if (!file.open(QFile::ReadOnly)) return QByteArray();
	return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Md5);
}

bool rangeInside(quint64 offset, quint64 size, quint64 total) {
	return offset <= total && size <= total - offset;
}

bool validUnit(std::span<const char> bytes, QString* error) {
	if (reinterpret_cast<quintptr>(bytes.data()) % static_cast<quintptr>(BUNDLE_ALIGNMENT) != 0) {
		*error = QStringLiteral("misaligned unit");
		return false;
	}

	if (bytes.size() < sizeof(QV4::CompiledData::Unit)) {
		*error = QStringLiteral("truncated unit");
		return false;
	}

	const auto* unit = reinterpret_cast<const QV4::CompiledData::Unit*>(bytes.data());

	if (std::memcmp(unit->magic, QV4::CompiledData::magic_str, sizeof(unit->magic)) != 0) {
		*error = QStringLiteral("bad unit magic");
		return false;
	}

	if (unit->version != quint32(QV4_DATA_STRUCTURE_VERSION)) {
		*error = QStringLiteral("unit data structure version %1, this Qt expects %2")
		             .arg(quint32(unit->version), 0, 16)
		             .arg(QV4_DATA_STRUCTURE_VERSION, 0, 16);
		return false;
	}

	if (unit->unitSize != bytes.size()) {
		*error = QStringLiteral("unit size mismatch");
		return false;
	}

	if (!(unit->flags & QV4::CompiledData::Unit::StaticData)) {
		*error = QStringLiteral("unit is not static data");
		return false;
	}

	if (unit->sourceTimeStamp != 0) {
		*error = QStringLiteral("unit carries a source time stamp");
		return false;
	}

	return true;
}

Bundle* parseBundle(std::span<const char> data, QString* error) {
	if (data.size() < sizeof(BundleHeader)) {
		*error = QStringLiteral("file too small");
		return nullptr;
	}

	auto header = BundleHeader();
	std::memcpy(&header, data.data(), sizeof(header));

	if (header.magic != BUNDLE_MAGIC) {
		*error = QStringLiteral("bad magic");
		return nullptr;
	}

	if (header.format != BUNDLE_FORMAT) {
		*error = QStringLiteral("unsupported format %1").arg(quint32(header.format));
		return nullptr;
	}

	auto qtVersion = QString::fromLatin1(
	    header.qtVersion.data(),
	    static_cast<qsizetype>(qstrnlen(header.qtVersion.data(), header.qtVersion.size()))
	);

	if (qtVersion != QLatin1StringView(qVersion())) {
		*error = QStringLiteral("built for Qt %1, running Qt %2")
		             .arg(qtVersion, QString::fromLatin1(qVersion()));
		return nullptr;
	}

	const quint64 count = header.count;
	if (!rangeInside(sizeof(BundleHeader), count * sizeof(BundleEntry), data.size())
	    || !rangeInside(header.stringsOffset, header.stringsSize, data.size()))
	{
		*error = QStringLiteral("index outside the file");
		return nullptr;
	}

	auto strings = data.subspan(header.stringsOffset, header.stringsSize);
	auto units = std::vector<Unit>(count);
	auto index = QHash<QString, size_t>();
	index.reserve(static_cast<qsizetype>(count));

	for (quint64 i = 0; i != count; i++) {
		auto entry = BundleEntry();
		auto entryBytes = data.subspan(sizeof(BundleHeader) + i * sizeof(BundleEntry), sizeof(entry));
		std::memcpy(&entry, entryBytes.data(), sizeof(entry));

		if (!rangeInside(entry.pathOffset, entry.pathSize, strings.size())) {
			*error = QStringLiteral("path outside the string table");
			return nullptr;
		}

		auto pathBytes = strings.subspan(entry.pathOffset, entry.pathSize);
		auto path = QString::fromUtf8(pathBytes.data(), static_cast<qsizetype>(pathBytes.size()));

		if (!rangeInside(entry.unitOffset, entry.unitSize, data.size())) {
			*error = path % QStringLiteral(": unit outside the file");
			return nullptr;
		}

		auto unitBytes = data.subspan(entry.unitOffset, entry.unitSize);

		QString unitError;
		if (!validUnit(unitBytes, &unitError)) {
			*error = path % QStringLiteral(": ") % unitError;
			return nullptr;
		}

		auto& unit = units[i];
		unit.cached.qmlData = reinterpret_cast<const QV4::CompiledData::Unit*>(unitBytes.data());
		unit.cached.aotCompiledFunctions = NO_AOT_FUNCTIONS.data();
		unit.sourceMd5 =
		    QByteArray(entry.sourceMd5.data(), static_cast<qsizetype>(entry.sourceMd5.size()));
		index.insert(path, static_cast<size_t>(i));
	}

	return new Bundle {.units = std::move(units), .index = std::move(index)};
}

Bundle* loadBundle(const QString& path) {
	auto timer = QElapsedTimer();
	timer.start();

	auto file = QFile(path);
	if (!file.open(QFile::ReadOnly)) {
		qCWarning(logQmlCache) << "Could not open QML bundle" << path << file.errorString();
		return nullptr;
	}

	auto size = file.size();
	if (size <= 0 || size > BUNDLE_MAX_SIZE) {
		qCWarning(logQmlCache) << "Ignoring QML bundle" << path << "with unexpected size" << size;
		return nullptr;
	}

	auto* data =
	    static_cast<char*>(::operator new(static_cast<size_t>(size), BUNDLE_ALIGNMENT, std::nothrow));

	if (!data) return nullptr;

	auto bytes = std::span<char>(data, static_cast<size_t>(size));

	if (file.read(bytes.data(), size) != size) {
		qCWarning(logQmlCache) << "Could not read QML bundle" << path << file.errorString();
		::operator delete(data, BUNDLE_ALIGNMENT);
		return nullptr;
	}

	QString error;
	auto* bundle = parseBundle(bytes, &error);
	if (!bundle) {
		qCWarning(logQmlCache).noquote() << "Ignoring QML bundle" << path << "-" << error;
		::operator delete(data, BUNDLE_ALIGNMENT);
		return nullptr;
	}

	qCInfo(logQmlCache) << "Loaded QML bundle with" << bundle->units.size() << "units from" << path
	                    << "in" << timer.elapsed() << "ms";
	return bundle;
}

const QQmlPrivate::CachedQmlUnit* lookup(const QUrl& url) {
	if (url.scheme() != QLatin1StringView("qs")) return nullptr;
	if (url.hasFragment() || url.hasQuery()) return nullptr;

	auto path = url.path();
	if (!path.startsWith(QLatin1StringView("@/qs/"))) return nullptr;

	auto relative = QDir::cleanPath(path.sliced(5));

	auto& state = globalState();
	auto locker = QMutexLocker(&state.mutex);

	if (!state.bundle) return nullptr;

	auto it = state.bundle->index.constFind(relative);
	if (it == state.bundle->index.constEnd()) {
		qCDebug(logQmlCache) << "No cached unit for" << relative;
		return nullptr;
	}

	auto file = QDir::cleanPath(state.root.filePath(relative));
	if (state.intercepts.contains(file)) {
		qCDebug(logQmlCache) << "Not using cached unit for preprocessed file" << relative;
		return nullptr;
	}

	auto hash = state.hashes.value(file);
	if (hash.isEmpty()) hash = hashFile(file);

	const auto& unit = state.bundle->units.at(*it);
	if (hash != unit.sourceMd5) {
		qCDebug(logQmlCache) << "Cached unit is stale for" << relative;
		return nullptr;
	}

	qCDebug(logQmlCache) << "Using cached unit for" << relative;
	return &unit.cached;
}

bool cacheDisabled() {
	static const bool disabled = qEnvironmentVariableIsSet("QS_DISABLE_QMLCACHE");
	return disabled;
}

BundleKey bundleKey(const QDir& configRoot) {
	if (!configRoot.isAbsolute()) return {};

	auto path = configRoot.filePath(QStringLiteral(".qmlcache/bundle.bin"));
	auto info = QFileInfo(path);
	if (!info.isFile()) return {};

	return {.path = path, .size = info.size(), .modified = info.lastModified()};
}

} // namespace

void preload(const QDir& configRoot) {
	if (cacheDisabled()) return;

	auto key = bundleKey(configRoot);
	if (key.path.isEmpty()) return;

	auto& state = globalState();
	auto locker = QMutexLocker(&state.mutex);

	if (key != state.key) {
		state.key = key;
		state.bundle = loadBundle(key.path);
	}
}

void activate(
    const QDir& configRoot,
    const QHash<QString, QByteArray>& fileHashes,
    const QHash<QString, QString>& fileIntercepts
) {
	if (cacheDisabled()) return;

	auto key = bundleKey(configRoot);

	auto hashes = QHash<QString, QByteArray>();
	auto intercepts = QSet<QString>();
	if (!key.path.isEmpty()) {
		hashes.reserve(fileHashes.size());
		for (auto it = fileHashes.constBegin(); it != fileHashes.constEnd(); ++it) {
			hashes.insert(QDir::cleanPath(it.key()), it.value());
		}

		for (auto it = fileIntercepts.constBegin(); it != fileIntercepts.constEnd(); ++it) {
			intercepts.insert(QDir::cleanPath(it.key()));
		}
	}

	auto& state = globalState();
	auto active = false;

	{
		auto locker = QMutexLocker(&state.mutex);

		if (key != state.key) {
			state.key = key;
			state.bundle = key.path.isEmpty() ? nullptr : loadBundle(key.path);
		}

		state.root = configRoot;
		state.hashes = std::move(hashes);
		state.intercepts = std::move(intercepts);
		active = state.bundle != nullptr;
	}

	if (!active) return;

	static std::once_flag registered;
	std::call_once(registered, [] {
		auto registration = QQmlPrivate::RegisterQmlUnitCacheHook {
		    .structVersion = 0,
		    .lookupCachedQmlUnit = &lookup,
		};

		QQmlPrivate::qmlregister(QQmlPrivate::QmlUnitCacheHookRegistration, &registration);
	});
}

} // namespace qs::qmlcache
