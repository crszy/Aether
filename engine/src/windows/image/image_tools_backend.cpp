#include "image_tools_backend.hpp"
#include <memory>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qstring.h>
#include <qthread.h>

#include "image_tools.hpp"
#include "image_tools_worker.hpp"

namespace qs::windows::image {

namespace {
Q_LOGGING_CATEGORY(logImageToolsBackend, "quickshell.windows.imagetools", QtWarningMsg);

constexpr unsigned long STOP_TIMEOUT_MS = 3000;
} // namespace

ImageToolsBackend::ImageToolsBackend(ImageTools* frontend): mThread(std::make_unique<QThread>()) {
	this->mWorker = new ImageToolsWorker(frontend);
	this->mWorker->moveToThread(this->mThread.get());
}

ImageToolsBackend::~ImageToolsBackend() {
	if (this->mThread->isRunning()) {
		this->mThread->quit();
		if (!this->mThread->wait(STOP_TIMEOUT_MS)) {
			qCWarning(logImageToolsBackend) << "Image tools worker didn't stop in time";
			(void) this->mThread.release();
			return;
		}
	}

	delete this->mWorker;
}

void ImageToolsBackend::requestLeastBusyRegion(
    int requestId,
    const QString& imagePath,
    int width,
    int height,
    int screenWidth,
    int screenHeight,
    int horizontalPadding,
    int verticalPadding,
    bool busiest
) {
	this->ensureStarted();

	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(
	    worker,
	    [worker,
	     requestId,
	     imagePath,
	     width,
	     height,
	     screenWidth,
	     screenHeight,
	     horizontalPadding,
	     verticalPadding,
	     busiest] {
		    worker->cmdLeastBusyRegion(
		        requestId,
		        imagePath,
		        width,
		        height,
		        screenWidth,
		        screenHeight,
		        horizontalPadding,
		        verticalPadding,
		        busiest
		    );
	    },
	    Qt::QueuedConnection
	);
}

void ImageToolsBackend::requestScheme(int requestId, const QString& imagePath) {
	this->ensureStarted();

	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(
	    worker,
	    [worker, requestId, imagePath] { worker->cmdScheme(requestId, imagePath); },
	    Qt::QueuedConnection
	);
}

void ImageToolsBackend::requestFindContentRegions(
    int requestId,
    const QString& imagePath,
    int minWidth,
    int minHeight,
    int maxWidth,
    int maxHeight
) {
	this->ensureStarted();

	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(
	    worker,
	    [worker, requestId, imagePath, minWidth, minHeight, maxWidth, maxHeight] {
		    worker->cmdFindContentRegions(requestId, imagePath, minWidth, minHeight, maxWidth, maxHeight);
	    },
	    Qt::QueuedConnection
	);
}

void ImageToolsBackend::ensureStarted() {
	if (this->mStarted) return;

	this->mStarted = true;
	this->mThread->start(QThread::LowPriority);
}

} // namespace qs::windows::image
