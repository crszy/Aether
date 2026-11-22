#include "ocr_backend.hpp"
#include <memory>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qstring.h>
#include <qthread.h>

#include "ocr.hpp"
#include "ocr_worker.hpp"

namespace qs::windows::sys {

namespace {
Q_LOGGING_CATEGORY(logOcrBackend, "quickshell.windows.ocr", QtWarningMsg);

constexpr unsigned long STOP_TIMEOUT_MS = 3000;
} // namespace

OcrBackend::OcrBackend(Ocr* frontend): mThread(std::make_unique<QThread>()) {
	this->mWorker = new OcrWorker(frontend);
	this->mWorker->moveToThread(this->mThread.get());

	QObject::connect(this->mThread.get(), &QThread::started, this->mWorker, &OcrWorker::start);
	QObject::connect(this->mThread.get(), &QThread::finished, this->mWorker, &OcrWorker::shutdown);
}

OcrBackend::~OcrBackend() {
	if (this->mThread->isRunning()) {
		this->mThread->quit();
		if (!this->mThread->wait(STOP_TIMEOUT_MS)) {
			qCWarning(logOcrBackend) << "OCR worker didn't stop in time";
			(void) this->mThread.release();
			return;
		}
	}

	delete this->mWorker;
}

void OcrBackend::requestRecognize(int requestId, const QString& path) {
	if (!this->mStarted) {
		this->mStarted = true;
		this->mThread->start();
	}

	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(
	    worker,
	    [worker, requestId, path] { worker->cmdRecognize(requestId, path); },
	    Qt::QueuedConnection
	);
}

} // namespace qs::windows::sys
