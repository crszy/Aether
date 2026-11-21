#include "art_cache.hpp"

#include <qbytearray.h>
#include <qcryptographichash.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qimage.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qstandardpaths.h>
#include <qstring.h>
#include <qurl.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt::Windows::Storage::Streams;

namespace qs::windows::services::mpris {

namespace {
Q_LOGGING_CATEGORY(logMprisArt, "quickshell.windows.mpris.art", QtWarningMsg);

constexpr qsizetype MAX_CACHED_FILES = 50;

QString cacheDir() {
	auto dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/mpris-art";
	QDir().mkpath(dir);
	return dir;
}

void pruneCache(const QString& dir) {
	QDir qdir(dir);
	auto entries = qdir.entryInfoList(QDir::Files, QDir::Time);
	for (qsizetype i = MAX_CACHED_FILES; i < entries.size(); i++) {
		QFile::remove(entries.at(i).absoluteFilePath());
	}
}
} // namespace

CachedArt cacheThumbnail(const IRandomAccessStreamReference& thumbnail) {
	if (thumbnail == nullptr) return {};

	try {
		auto stream = thumbnail.OpenReadAsync().get();
		auto size = static_cast<uint32_t>(stream.Size());
		if (size == 0) return {};

		DataReader reader(stream);
		reader.LoadAsync(size).get();

		QByteArray bytes(static_cast<qsizetype>(size), Qt::Uninitialized);
		reader.ReadBytes(winrt::array_view<uint8_t>(
		    reinterpret_cast<uint8_t*>(bytes.data()), // NOLINT
		    reinterpret_cast<uint8_t*>(bytes.data()) + bytes.size() // NOLINT
		));

		auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex();
		auto dir = cacheDir();
		auto path = dir + "/" + hash + ".png";
		if (QFileInfo::exists(path)) return {QUrl::fromLocalFile(path).toString(), hash};

		QImage image;
		if (!image.loadFromData(bytes)) {
			qCWarning(logMprisArt) << "Could not decode a thumbnail of" << size << "bytes";
			return {};
		}

		if (!image.save(path, "PNG")) {
			qCWarning(logMprisArt) << "Could not save thumbnail to" << path;
			return {};
		}

		pruneCache(dir);
		return {QUrl::fromLocalFile(path).toString(), hash};
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMprisArt) << "Failed reading thumbnail:" << QString::fromWCharArray(e.message().c_str());
		return {};
	}
}

} // namespace qs::windows::services::mpris
