#include "thumbnailer.hpp"

#include <algorithm>
#include <cstring>

#include <qt_windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>

#include <qcoreapplication.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qimage.h>
#include <qimagereader.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qpointer.h>
#include <qrunnable.h>
#include <qsize.h>
#include <qstringlist.h>
#include <qthreadpool.h>
#include <quuid.h>

#include <winrt/base.h>

#include "../../core/backgroundpool.hpp"

namespace qs::windows::image {

namespace {
Q_LOGGING_CATEGORY(logThumbnailer, "quickshell.windows.thumbnailer", QtWarningMsg);

bool saveAtomically(const QImage& image, const QString& outputPath) {
	auto dir = QFileInfo(outputPath).absoluteDir();
	if (!dir.exists() && !dir.mkpath(".")) {
		qCWarning(logThumbnailer) << "could not create" << dir.absolutePath();
		return false;
	}

	auto tempPath = outputPath + u".tmp-" + QUuid::createUuid().toString(QUuid::Id128);
	if (!image.save(tempPath, "PNG")) {
		qCWarning(logThumbnailer) << "could not write" << tempPath;
		QFile::remove(tempPath);
		return false;
	}

	QFile::remove(outputPath);
	if (!QFile::rename(tempPath, outputPath)) {
		qCWarning(logThumbnailer) << "could not move" << tempPath << "to" << outputPath;
		QFile::remove(tempPath);
		return false;
	}

	return true;
}

bool isVideoFile(const QString& path) {
	static const QStringList suffixes = {"mp4", "webm", "mkv", "avi", "mov", "wmv", "m4v"};
	return suffixes.contains(QFileInfo(path).suffix().toLower());
}

QImage readVideoFrame(const QString& path) {
	auto com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
	if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
		if (com) CoUninitialize();
		return {};
	}

	QImage image;
	[&] {
		winrt::com_ptr<IMFAttributes> attributes;
		if (FAILED(MFCreateAttributes(attributes.put(), 1))) return;
		attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

		auto url = QDir::toNativeSeparators(path).toStdWString();
		winrt::com_ptr<IMFSourceReader> reader;
		auto hr = MFCreateSourceReaderFromURL(url.c_str(), attributes.get(), reader.put());
		if (FAILED(hr)) {
			qCWarning(logThumbnailer) << "could not open video" << path << ":" << Qt::hex << hr;
			return;
		}

		const auto stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
		reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
		reader->SetStreamSelection(stream, TRUE);

		winrt::com_ptr<IMFMediaType> wanted;
		if (FAILED(MFCreateMediaType(wanted.put()))) return;
		wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		wanted->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
		hr = reader->SetCurrentMediaType(stream, nullptr, wanted.get());
		if (FAILED(hr)) {
			qCWarning(logThumbnailer) << "no RGB32 output for" << path << ":" << Qt::hex << hr;
			return;
		}

		winrt::com_ptr<IMFMediaType> current;
		if (FAILED(reader->GetCurrentMediaType(stream, current.put()))) return;
		UINT32 width = 0;
		UINT32 height = 0;
		if (FAILED(MFGetAttributeSize(current.get(), MF_MT_FRAME_SIZE, &width, &height)) || width == 0
		    || height == 0)
		{
			return;
		}
		auto stride =
		    static_cast<LONG>(MFGetAttributeUINT32(current.get(), MF_MT_DEFAULT_STRIDE, width * 4));

		PROPVARIANT duration;
		PropVariantInit(&duration);
		LONGLONG position = 10'000'000;
		if (SUCCEEDED(reader->GetPresentationAttribute(
		        static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),
		        MF_PD_DURATION,
		        &duration
		    ))
		    && duration.vt == VT_UI8)
		{
			position = std::min<LONGLONG>(position, static_cast<LONGLONG>(duration.uhVal.QuadPart / 10));
		}
		PropVariantClear(&duration);

		PROPVARIANT seek;
		InitPropVariantFromInt64(position, &seek);
		reader->SetCurrentPosition(GUID_NULL, seek);
		PropVariantClear(&seek);

		for (auto attempt = 0; attempt < 30 && image.isNull(); attempt++) {
			DWORD flags = 0;
			LONGLONG timestamp = 0;
			winrt::com_ptr<IMFSample> sample;
			if (FAILED(reader->ReadSample(stream, 0, nullptr, &flags, &timestamp, sample.put()))) return;
			if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) return;
			if (!sample) continue;

			winrt::com_ptr<IMFMediaBuffer> buffer;
			if (FAILED(sample->ConvertToContiguousBuffer(buffer.put()))) return;

			BYTE* data = nullptr;
			DWORD length = 0;
			if (FAILED(buffer->Lock(&data, nullptr, &length))) return;

			auto rowBytes = static_cast<qsizetype>(width) * 4;
			auto absStride = static_cast<qsizetype>(stride < 0 ? -stride : stride);
			if (absStride >= rowBytes && static_cast<qsizetype>(length) >= absStride * height) {
				image = QImage(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGB32);
				for (UINT32 y = 0; y < height; y++) {
					auto row = stride >= 0 ? y : height - 1 - y;
					std::memcpy(image.scanLine(static_cast<int>(y)), data + row * absStride, rowBytes);
				}
			}

			buffer->Unlock();
		}
	}();

	MFShutdown();
	if (com) CoUninitialize();
	return image;
}

bool writeVideoThumbnail(const QString& sourcePath, const QString& outputPath, int maxSize) {
	auto image = readVideoFrame(sourcePath);
	if (image.isNull()) {
		qCWarning(logThumbnailer) << "could not read a frame of" << sourcePath;
		return false;
	}

	if (image.width() > maxSize || image.height() > maxSize) {
		image = image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}

	return saveAtomically(image, outputPath);
}

bool writeThumbnail(const QString& sourcePath, const QString& outputPath, int maxSize) {
	if (isVideoFile(sourcePath)) return writeVideoThumbnail(sourcePath, outputPath, maxSize);

	QImageReader reader(sourcePath);
	reader.setAutoTransform(true);

	auto size = reader.size();
	if (size.isValid()) {
		reader.setScaledSize(size.scaled(maxSize, maxSize, Qt::KeepAspectRatio));
	}

	auto image = reader.read();
	if (image.isNull()) {
		qCWarning(logThumbnailer) << "could not decode" << sourcePath << ":" << reader.errorString();
		return false;
	}

	if (!size.isValid()) {
		image = image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}

	return saveAtomically(image, outputPath);
}

class ThumbnailTask: public QRunnable {
public:
	ThumbnailTask(Thumbnailer* owner, QString source, QString output, int maxSize)
	    : owner(owner)
	    , source(std::move(source))
	    , output(std::move(output))
	    , maxSize(maxSize) {}

	void run() override {
		auto ok = writeThumbnail(this->source, this->output, this->maxSize);
		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [owner = this->owner, source = this->source, output = this->output, ok] {
			    if (owner) emit owner->finished(source, output, ok);
		    },
		    Qt::QueuedConnection
		);
	}

private:
	QPointer<Thumbnailer> owner;
	QString source;
	QString output;
	int maxSize;
};

} // namespace

void Thumbnailer::generate(const QString& sourcePath, const QString& outputPath, int maxSize) {
	if (QFileInfo::exists(outputPath)) {
		emit this->finished(sourcePath, outputPath, true);
		return;
	}

	auto* task = new ThumbnailTask(this, sourcePath, outputPath, maxSize); // NOLINT
	task->setAutoDelete(true);
	BackgroundThreadPool::instance()->start(task);
}

} // namespace qs::windows::image
