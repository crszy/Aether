#include "file_index.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <qt_windows.h>

#include <knownfolders.h>
#include <shlobj.h>

#include <qcoreapplication.h>
#include <qdir.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qpointer.h>
#include <qrunnable.h>
#include <qstringview.h>
#include <qthreadpool.h>

#include "../../core/backgroundpool.hpp"
#include "../../core/logcat.hpp"

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logFileIndex, "quickshell.windows.fileindex", QtWarningMsg);
}

struct FileIndexEntry {
	QString name;
	QString nameLower;
	QString path;
	qint64 size = 0;
	qint64 modified = 0;
	int depth = 0;
	bool isDir = false;
};

struct FileIndexData {
	std::vector<FileIndexEntry> entries;
	std::chrono::steady_clock::time_point builtAt;
};

namespace {

constexpr size_t MAX_ENTRIES = 300000;
constexpr int MAX_DEPTH = 8;
constexpr auto REBUILD_AGE = std::chrono::seconds(60);

std::mutex gMutex;
std::shared_ptr<const FileIndexData> gData;
bool gBuilding = false;

qint64 fileTimeToUnixSeconds(const FILETIME& ft) {
	ULARGE_INTEGER value {};
	value.LowPart = ft.dwLowDateTime;
	value.HighPart = ft.dwHighDateTime;

	constexpr qint64 EPOCH_DIFF_100NS = 116444736000000000LL;
	auto ticks = static_cast<qint64>(value.QuadPart) - EPOCH_DIFF_100NS;
	return ticks > 0 ? ticks / 10000000LL : 0;
}

QString toDisplayPath(const wchar_t* native) {
	auto path = QString::fromWCharArray(native);
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));
	while (path.size() > 3 && path.endsWith(QLatin1Char('/'))) path.chop(1);
	return path;
}

std::wstring toSearchPattern(const QString& displayDir) {
	auto native = QDir::toNativeSeparators(displayDir);
	auto wide = native.toStdWString();

	if (wide.rfind(L"\\\\?\\", 0) != 0) {
		if (wide.size() >= 2 && wide[0] == L'\\' && wide[1] == L'\\') {
			wide = L"\\\\?\\UNC\\" + wide.substr(2);
		} else {
			wide = L"\\\\?\\" + wide;
		}
	}

	return wide + L"\\*";
}

bool isInsidePath(const QString& path, const QString& parent) {
	if (path.size() < parent.size()) return false;
	if (QStringView(path).left(parent.size()).compare(parent, Qt::CaseInsensitive) != 0) {
		return false;
	}
	if (path.size() == parent.size()) return true;

	auto next = path.at(parent.size());
	return next == u'/' || next == u'\\';
}

bool shouldSkipDirName(const QString& name) {
	return name == QStringLiteral("AppData") || name == QStringLiteral("node_modules")
	    || name == QStringLiteral("__pycache__");
}

struct WalkContext {
	std::vector<FileIndexEntry>& out;
	bool capped = false;
};

void walkDir(const QString& displayDir, int depthRemaining, WalkContext& ctx) {
	if (ctx.out.size() >= MAX_ENTRIES) {
		ctx.capped = true;
		return;
	}

	auto pattern = toSearchPattern(displayDir);
	WIN32_FIND_DATAW findData {};
	auto handle = FindFirstFileExW(
	    pattern.c_str(),
	    FindExInfoBasic,
	    &findData,
	    FindExSearchNameMatch,
	    nullptr,
	    FIND_FIRST_EX_LARGE_FETCH
	);
	if (handle == INVALID_HANDLE_VALUE) return;

	do {
		std::wstring_view name(findData.cFileName);
		if (name == L"." || name == L"..") continue;
		if (name.empty()) continue;

		auto attrs = findData.dwFileAttributes;
		if ((attrs & FILE_ATTRIBUTE_HIDDEN) != 0) continue;
		if ((attrs & FILE_ATTRIBUTE_SYSTEM) != 0) continue;
		if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(findData.dwReserved0)) {
			continue;
		}
		if (name[0] == L'.') continue;

		auto isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
		auto qname = QString::fromWCharArray(name.data(), static_cast<int>(name.size()));
		if (isDir && shouldSkipDirName(qname)) continue;

		auto displayPath = displayDir + u'/' + qname;

		FileIndexEntry entry;
		entry.name = qname;
		entry.nameLower = qname.toLower();
		entry.path = displayPath;
		entry.isDir = isDir;
		entry.depth = static_cast<int>(displayPath.count(u'/'));
		entry.modified = fileTimeToUnixSeconds(findData.ftLastWriteTime);
		if (!isDir) {
			ULARGE_INTEGER size {};
			size.LowPart = findData.nFileSizeLow;
			size.HighPart = findData.nFileSizeHigh;
			entry.size = static_cast<qint64>(size.QuadPart);
		}

		ctx.out.push_back(std::move(entry));
		if (ctx.out.size() >= MAX_ENTRIES) {
			ctx.capped = true;
			break;
		}

		if (isDir && depthRemaining > 1) {
			walkDir(displayPath, depthRemaining - 1, ctx);
			if (ctx.capped) break;
		}
	} while (FindNextFileW(handle, &findData));

	FindClose(handle);
}

void collectRoots(std::vector<QString>& roots) {
	PWSTR raw = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &raw)) || raw == nullptr) {
		qCWarning(logFileIndex) << "SHGetKnownFolderPath(FOLDERID_Profile) failed";
		return;
	}
	roots.push_back(toDisplayPath(raw));
	CoTaskMemFree(raw);

	static const GUID* const EXTRA_FOLDERS[] = {
	    &FOLDERID_Desktop,
	    &FOLDERID_Documents,
	    &FOLDERID_Downloads,
	    &FOLDERID_Pictures,
	    &FOLDERID_Music,
	    &FOLDERID_Videos,
	};

	for (const auto* id: EXTRA_FOLDERS) {
		PWSTR extraRaw = nullptr;
		if (FAILED(SHGetKnownFolderPath(*id, 0, nullptr, &extraRaw)) || extraRaw == nullptr) continue;
		auto extra = toDisplayPath(extraRaw);
		CoTaskMemFree(extraRaw);

		auto insideExisting =
		    std::ranges::any_of(roots, [&](const QString& root) { return isInsidePath(extra, root); });
		if (insideExisting) continue;

		roots.push_back(extra);
	}
}

std::shared_ptr<const FileIndexData> buildIndex() {
	auto result = std::make_shared<FileIndexData>();
	result->entries.reserve(65536);

	std::vector<QString> roots;
	collectRoots(roots);

	WalkContext ctx {result->entries, false};
	for (const auto& root: roots) {
		if (ctx.out.size() >= MAX_ENTRIES) break;
		walkDir(root, MAX_DEPTH, ctx);
	}

	result->builtAt = std::chrono::steady_clock::now();
	return result;
}

} // namespace

FileIndex::FileIndex(QObject* parent): QObject(parent) { this->syncFromGlobal(); }

FileIndex::~FileIndex() = default;

void FileIndex::syncFromGlobal() {
	std::shared_ptr<const FileIndexData> snapshot;
	bool building = false;
	{
		auto lock = std::scoped_lock(gMutex);
		snapshot = gData;
		building = gBuilding;
	}

	auto emitCount = false;
	auto emitIndexing = false;
	auto emitIndex = false;

	if (snapshot != this->data) {
		emitIndex = true;
		this->data = snapshot;

		auto newCount = snapshot ? static_cast<int>(snapshot->entries.size()) : 0;
		if (newCount != this->mCount) {
			this->mCount = newCount;
			emitCount = true;
		}
	}

	if (building != this->mIndexing) {
		this->mIndexing = building;
		emitIndexing = true;
	}

	if (emitCount) emit this->countChanged();
	if (emitIndexing) emit this->indexingChanged();
	if (emitIndex) emit this->indexChanged();
}

void FileIndex::ensureIndexed() {
	this->syncFromGlobal();
	if (this->mIndexing) return;

	auto stale = !this->data;
	if (!stale) {
		auto age = std::chrono::steady_clock::now() - this->data->builtAt;
		stale = age >= REBUILD_AGE;
	}

	if (stale) this->startBuild();
}

void FileIndex::startBuild() {
	{
		auto lock = std::scoped_lock(gMutex);
		if (gBuilding) return;
		gBuilding = true;
	}

	this->syncFromGlobal();

	auto guard = QPointer<FileIndex>(this);
	auto* task = QRunnable::create([guard]() {
		auto result = buildIndex();

		{
			auto lock = std::scoped_lock(gMutex);
			gData = result;
			gBuilding = false;
		}

		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [guard]() {
			    auto* self = guard.data();
			    if (self == nullptr) return;
			    self->syncFromGlobal();
		    },
		    Qt::QueuedConnection
		);
	});

	BackgroundThreadPool::instance()->start(task);
}

QVariantList FileIndex::search(const QString& query, int limit) const {
	auto needle = query.trimmed().toLower();
	if (needle.isEmpty() || limit <= 0 || !this->data) return {};

	const auto& entries = this->data->entries;

	std::vector<int> matches;
	matches.reserve(std::min<size_t>(entries.size(), 1024));

	for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
		if (entries[static_cast<size_t>(i)].nameLower.contains(needle)) matches.push_back(i);
	}

	auto better = [&](int a, int b) {
		const auto& ea = entries[static_cast<size_t>(a)];
		const auto& eb = entries[static_cast<size_t>(b)];

		auto startsA = ea.nameLower.startsWith(needle);
		auto startsB = eb.nameLower.startsWith(needle);
		if (startsA != startsB) return startsA;

		if (ea.depth != eb.depth) return ea.depth < eb.depth;

		if (ea.name.length() != eb.name.length()) return ea.name.length() < eb.name.length();

		return ea.nameLower < eb.nameLower;
	};

	auto take = std::min<size_t>(matches.size(), static_cast<size_t>(limit));
	if (matches.size() > take) {
		std::nth_element(matches.begin(), matches.begin() + static_cast<long>(take), matches.end(), better);
		matches.resize(take);
	}
	std::sort(matches.begin(), matches.end(), better);

	QVariantList result;
	result.reserve(static_cast<int>(matches.size()));
	for (auto index: matches) {
		const auto& entry = entries[static_cast<size_t>(index)];
		QVariantMap map;
		map.insert(QStringLiteral("name"), entry.name);
		map.insert(QStringLiteral("path"), entry.path);
		map.insert(QStringLiteral("isDir"), entry.isDir);
		map.insert(QStringLiteral("size"), static_cast<double>(entry.size));
		map.insert(QStringLiteral("modified"), static_cast<double>(entry.modified));
		result.append(map);
	}
	return result;
}

} // namespace qs::windows::sys
