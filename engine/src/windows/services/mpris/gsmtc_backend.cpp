#include "gsmtc_backend.hpp"
#include <memory>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qthread.h>
#include <qtypes.h>

#include "../../startup.hpp"
#include "gsmtc_worker.hpp"
#include "mpris.hpp"

namespace qs::windows::services::mpris {

namespace {
Q_LOGGING_CATEGORY(logMprisBackend, "quickshell.windows.mpris", QtWarningMsg);

constexpr unsigned long STOP_TIMEOUT_MS = 3000;
} // namespace

GsmtcBackend::GsmtcBackend(Mpris* frontend): mThread(std::make_unique<QThread>()) {
	this->mWorker = new GsmtcWorker(frontend);
	this->mWorker->moveToThread(this->mThread.get());

	QObject::connect(this->mThread.get(), &QThread::started, this->mWorker, &GsmtcWorker::start);
	QObject::connect(this->mThread.get(), &QThread::finished, this->mWorker, &GsmtcWorker::shutdown);

	startup::afterFirstFrame(this, [this]() { this->mThread->start(); });
}

GsmtcBackend::~GsmtcBackend() {
	this->mThread->quit();
	if (!this->mThread->wait(STOP_TIMEOUT_MS)) {
		qCWarning(logMprisBackend) << "Media session worker didn't stop in time";
		(void) this->mThread.release();
		return;
	}

	delete this->mWorker;
}

void GsmtcBackend::play(quint64 sessionId) {
	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(worker, [worker, sessionId] { worker->cmdPlay(sessionId); }, Qt::QueuedConnection);
}

void GsmtcBackend::pause(quint64 sessionId) {
	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(worker, [worker, sessionId] { worker->cmdPause(sessionId); }, Qt::QueuedConnection);
}

void GsmtcBackend::togglePlayPause(quint64 sessionId) {
	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(
	    worker,
	    [worker, sessionId] { worker->cmdTogglePlayPause(sessionId); },
	    Qt::QueuedConnection
	);
}

void GsmtcBackend::next(quint64 sessionId) {
	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(worker, [worker, sessionId] { worker->cmdNext(sessionId); }, Qt::QueuedConnection);
}

void GsmtcBackend::previous(quint64 sessionId) {
	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(worker, [worker, sessionId] { worker->cmdPrevious(sessionId); }, Qt::QueuedConnection);
}

void GsmtcBackend::setPosition(quint64 sessionId, qint64 ticks) {
	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(
	    worker,
	    [worker, sessionId, ticks] { worker->cmdSetPosition(sessionId, ticks); },
	    Qt::QueuedConnection
	);
}

void GsmtcBackend::setShuffle(quint64 sessionId, bool shuffle) {
	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(
	    worker,
	    [worker, sessionId, shuffle] { worker->cmdSetShuffle(sessionId, shuffle); },
	    Qt::QueuedConnection
	);
}

void GsmtcBackend::setLoopState(quint64 sessionId, MprisLoopState::Enum state) {
	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(
	    worker,
	    [worker, sessionId, state] { worker->cmdSetLoopState(sessionId, state); },
	    Qt::QueuedConnection
	);
}

} // namespace qs::windows::services::mpris
