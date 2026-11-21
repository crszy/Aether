#pragma once

#include <qdatetime.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qimage.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "screenshot_jobs.hpp"

namespace qs::windows::sys {

class Screenshot: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Screenshot(QObject* parent = nullptr);

	Q_INVOKABLE bool captureScreen(const QString& screenName, const QString& path);
	Q_INVOKABLE int captureScreenAsync(const QString& screenName, const QString& path);
	Q_INVOKABLE void cancelCapture(int requestId);

	Q_INVOKABLE bool
	cropToFile(const QString& srcPath, int x, int y, int width, int height, const QString& dstPath);

	Q_INVOKABLE QString pixelAt(const QString& path, int x, int y);
	Q_INVOKABLE void releasePixels();
	Q_INVOKABLE void releaseCapture(const QString& path);
	Q_INVOKABLE void discardCapture(const QString& path);

signals:
	void captureFinished(int requestId, bool ok);

private:
	struct KeptCapture {
		QImage image;
		QDateTime modified;
		qint64 size = -1;
	};

	void finishCaptureRequest(int requestId, bool ok, const QImage& image);
	bool loadPixels(const QString& path);
	void keepCapture(const QString& path, const QImage& image);
	QImage takeCapture(const QFileInfo& info);

	QString pixelsPath;
	QDateTime pixelsModified;
	qint64 pixelsFileSize = -1;
	QImage pixels;

	QHash<QString, KeptCapture> captures;
	QTimer capturesExpiry;
	ScreenshotJobs jobs;
};

} // namespace qs::windows::sys
