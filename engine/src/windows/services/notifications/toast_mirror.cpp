#include "toast_mirror.hpp"

#include <qcoreapplication.h>
#include <qdeadlinetimer.h>
#include <qlogging.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qthread.h>

#include "toast_worker.hpp"

namespace qs::windows::services::notifications {

QString SystemNotificationAccess::toString(SystemNotificationAccess::Enum value) {
	switch (value) {
	case SystemNotificationAccess::Unknown: return "Unknown";
	case SystemNotificationAccess::Unspecified: return "Unspecified";
	case SystemNotificationAccess::Allowed: return "Allowed";
	case SystemNotificationAccess::Denied: return "Denied";
	case SystemNotificationAccess::Unavailable: return "Unavailable";
	default: return "Invalid system notification access";
	}
}

ToastMirror::ToastMirror(QObject* parent): QObject(parent) {
	this->mWorker = new ToastMirrorWorker(this);
	this->mWorker->moveToThread(&this->mThread);

	QObject::connect(&this->mThread, &QThread::started, this->mWorker, &ToastMirrorWorker::start);
	QObject::connect(&this->mThread, &QThread::finished, this->mWorker, &ToastMirrorWorker::shutdown);

	QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
		this->mThread.quit();
		if (!this->mThread.wait(QDeadlineTimer(2000))) {
			qWarning() << "Notification listener thread still busy at exit, leaving it behind";
		}
	});

	this->mThread.setObjectName(QStringLiteral("ToastMirror"));
	this->mThread.start();
}

ToastMirror::~ToastMirror() {
	this->mThread.quit();
	this->mThread.wait();
	delete this->mWorker;
}

void ToastMirror::setEnabled(bool enabled) {
	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(worker, [worker, enabled] { worker->setEnabled(enabled); }, Qt::QueuedConnection);
}

void ToastMirror::requestAccess() {
	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(worker, [worker] { worker->requestAccess(); }, Qt::QueuedConnection);
}

void ToastMirror::removeToast(quint32 id) {
	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(worker, [worker, id] { worker->removeToast(id); }, Qt::QueuedConnection);
}

void ToastMirror::workerState(SystemNotificationAccess::Enum access, bool active) {
	if (access != this->mAccess) {
		this->mAccess = access;
		emit this->accessChanged();
	}

	if (active != this->mActive) {
		this->mActive = active;
		emit this->activeChanged();
	}
}

} // namespace qs::windows::services::notifications
