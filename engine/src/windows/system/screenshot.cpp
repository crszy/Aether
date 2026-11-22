#include "screenshot.hpp"

#include <qt_windows.h>

#include <qcolor.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qcoreapplication.h>
#include <qguiapplication.h>
#include <qimage.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qpainter.h>
#include <qpointer.h>
#include <qrect.h>
#include <qscreen.h>
#include <qstring.h>
#include <qthreadpool.h>
#include <qurl.h>

#include "../capture.hpp"
#include "../util.hpp"

namespace qs::windows::sys {

namespace {
Q_LOGGING_CATEGORY(logScreenshot, "quickshell.windows.screenshot", QtWarningMsg);

constexpr int CAPTURE_TIMEOUT_MS = 2000;
constexpr int PNG_QUALITY = 85;
constexpr int KEEP_CAPTURE_MS = 30000;

QString localPath(const QString& path) {
	if (path.startsWith("file:", Qt::CaseInsensitive)) return QUrl(path).toLocalFile();
	return path;
}
} // namespace

Screenshot::Screenshot(QObject* parent): QObject(parent) {
	this->capturesExpiry.setSingleShot(true);
	this->capturesExpiry.setInterval(KEEP_CAPTURE_MS);
	QObject::connect(&this->capturesExpiry, &QTimer::timeout, this, [this]() {
		this->captures.clear();
	});
}

bool Screenshot::captureScreen(const QString& screenName, const QString& path) {
	QList<QScreen*> screens;

	for (auto* screen: QGuiApplication::screens()) {
		if (screenName.isEmpty() || screen->name() == screenName) screens.append(screen);
	}

	if (screens.isEmpty()) {
		qCWarning(logScreenshot) << "No screen named" << screenName;
		return false;
	}

	struct Shot {
		QRect rect;
		QImage image;
	};

	QList<Shot> shots;
	QRect bounds;

	for (auto* screen: screens) {
		auto* monitor = monitorForScreen(screen);
		auto rects = monitorRects(monitor);

		if (monitor == nullptr || !rects.valid) {
			qCWarning(logScreenshot) << "No monitor for screen" << screen->name();
			return false;
		}

		auto image = capture::CaptureThread::instance()->grabMonitor(monitor, CAPTURE_TIMEOUT_MS);
		if (image.isNull()) {
			qCWarning(logScreenshot) << "Capturing" << screen->name() << "timed out or failed";
			return false;
		}

		shots.append({.rect = QRect(rects.monitor.topLeft(), image.size()), .image = image});
		bounds = bounds.united(shots.last().rect);
	}

	QImage result;

	if (shots.size() == 1) {
		result = shots.first().image;
	} else {
		result = QImage(bounds.size(), QImage::Format_RGBX8888);
		result.fill(Qt::black);
		QPainter painter(&result);
		for (const auto& shot: shots) painter.drawImage(shot.rect.topLeft() - bounds.topLeft(), shot.image);
	}

	auto info = QFileInfo(path);
	if (!QDir().mkpath(info.absolutePath())) {
		qCWarning(logScreenshot) << "Cannot create" << info.absolutePath();
		return false;
	}

	this->jobs.supersede(path);

	if (!result.save(path, "PNG", PNG_QUALITY)) {
		qCWarning(logScreenshot) << "Cannot write" << path;
		return false;
	}

	this->keepCapture(path, result);
	return true;
}

int Screenshot::captureScreenAsync(const QString& screenName, const QString& path) {
	struct Target {
		HMONITOR monitor = nullptr;
		QPoint topLeft;
		QString name;
	};

	QList<Target> targets;
	for (auto* screen: QGuiApplication::screens()) {
		if (!screenName.isEmpty() && screen->name() != screenName) continue;

		auto* monitor = monitorForScreen(screen);
		auto rects = monitorRects(monitor);
		if (monitor == nullptr || !rects.valid) {
			qCWarning(logScreenshot) << "No monitor for screen" << screen->name();
			return -1;
		}

		targets.append({.monitor = monitor, .topLeft = rects.monitor.topLeft(), .name = screen->name()});
	}

	if (targets.isEmpty()) {
		qCWarning(logScreenshot) << "No screen named" << screenName;
		return -1;
	}

	capture::CaptureThread::instance();
	auto job = this->jobs.start(localPath(path));

	QThreadPool::globalInstance()->start([self = QPointer(this), job, targets]() {
		QImage result;
		QRect bounds;
		QList<QPair<QPoint, QImage>> shots;
		auto ok = true;

		for (const auto& target: targets) {
			if (job.isCancelled()) {
				ok = false;
				break;
			}

			auto image = capture::CaptureThread::instance()->grabMonitor(target.monitor, CAPTURE_TIMEOUT_MS);
			if (image.isNull()) {
				qCWarning(logScreenshot) << "Capturing" << target.name << "timed out or failed";
				ok = false;
				break;
			}

			bounds = bounds.united(QRect(target.topLeft, image.size()));
			shots.append({target.topLeft, image});
		}

		if (ok && !job.isCancelled()) {
			if (shots.size() == 1) {
				result = shots.first().second;
			} else {
				result = QImage(bounds.size(), QImage::Format_RGBX8888);
				result.fill(Qt::black);
				QPainter painter(&result);
				for (const auto& [topLeft, image]: shots) painter.drawImage(topLeft - bounds.topLeft(), image);
			}

			ok = ScreenshotJobs::write(job, result, PNG_QUALITY);
		} else {
			ok = false;
		}

		auto* app = QCoreApplication::instance();
		if (app == nullptr) {
			QFile::remove(job.partPath);
			return;
		}

		QMetaObject::invokeMethod(
		    app,
		    [self, job, ok, result]() {
			    if (self) self->finishCaptureRequest(job.id, ok, result);
			    else QFile::remove(job.partPath);
		    },
		    Qt::QueuedConnection
		);
	});

	return job.id;
}

void Screenshot::finishCaptureRequest(int requestId, bool ok, const QImage& image) {
	auto path = this->jobs.finish(requestId, ok);
	if (!path.isEmpty()) this->keepCapture(path, image);
	emit this->captureFinished(requestId, !path.isEmpty());
}

void Screenshot::cancelCapture(int requestId) { this->jobs.cancel(requestId); }

bool Screenshot::cropToFile(
    const QString& srcPath,
    int x,
    int y,
    int width,
    int height,
    const QString& dstPath
) {
	auto source = this->takeCapture(QFileInfo(srcPath));
	if (source.isNull()) source = QImage(srcPath);

	if (source.isNull()) {
		qCWarning(logScreenshot) << "Cannot load" << srcPath;
		return false;
	}

	auto rect = QRect(x, y, width, height).intersected(source.rect());
	if (rect.isEmpty()) {
		qCWarning(logScreenshot) << "Crop rect" << QRect(x, y, width, height)
		                          << "is empty after clamping to" << source.rect();
		return false;
	}

	auto cropped = source.copy(rect);

	auto info = QFileInfo(dstPath);
	if (!QDir().mkpath(info.absolutePath())) {
		qCWarning(logScreenshot) << "Cannot create" << info.absolutePath();
		return false;
	}

	this->captures.remove(info.absoluteFilePath());
	this->jobs.supersede(dstPath);

	if (!cropped.save(dstPath, "PNG", PNG_QUALITY)) {
		qCWarning(logScreenshot) << "Cannot write" << dstPath;
		return false;
	}

	return true;
}

bool Screenshot::loadPixels(const QString& path) {
	auto info = QFileInfo(path);
	if (!info.exists()) {
		qCWarning(logScreenshot) << "pixelAt: no file at" << path;
		this->releasePixels();
		return false;
	}

	auto modified = info.lastModified();
	auto size = info.size();
	if (!this->pixels.isNull() && path == this->pixelsPath && modified == this->pixelsModified
	    && size == this->pixelsFileSize)
	{
		return true;
	}

	auto image = this->takeCapture(info);
	if (image.isNull()) image = QImage(path);

	if (image.isNull()) {
		qCWarning(logScreenshot) << "pixelAt: cannot load" << path;
		this->releasePixels();
		return false;
	}

	this->pixels = image.convertToFormat(QImage::Format_RGB32);
	this->pixelsPath = path;
	this->pixelsModified = modified;
	this->pixelsFileSize = size;
	return true;
}

QString Screenshot::pixelAt(const QString& path, int x, int y) {
	if (!this->loadPixels(localPath(path))) return QString();
	if (!this->pixels.valid(x, y)) return QString();
	return QColor::fromRgb(this->pixels.pixel(x, y)).name(QColor::HexRgb);
}

void Screenshot::keepCapture(const QString& path, const QImage& image) {
	auto info = QFileInfo(path);

	this->captures.insert(
	    info.absoluteFilePath(),
	    {.image = image, .modified = info.lastModified(), .size = info.size()}
	);

	this->capturesExpiry.start();
}

QImage Screenshot::takeCapture(const QFileInfo& info) {
	auto kept = this->captures.take(info.absoluteFilePath());
	if (kept.image.isNull() || !info.exists()) return {};
	if (info.lastModified() != kept.modified || info.size() != kept.size) return {};
	return kept.image;
}

void Screenshot::releaseCapture(const QString& path) {
	this->captures.remove(QFileInfo(localPath(path)).absoluteFilePath());
	if (this->captures.isEmpty()) this->capturesExpiry.stop();
}

void Screenshot::discardCapture(const QString& path) {
	this->releaseCapture(path);
	this->jobs.discard(localPath(path));
}

void Screenshot::releasePixels() {
	this->pixels = QImage();
	this->pixelsPath.clear();
	this->pixelsModified = QDateTime();
	this->pixelsFileSize = -1;
}

} // namespace qs::windows::sys
