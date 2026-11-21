#include "bt_backend.hpp"

#include <memory>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qstring.h>
#include <qthread.h>
#include <qtypes.h>

#include "../startup.hpp"
#include "bt_worker.hpp"

namespace qs::bluetooth {

namespace {
Q_LOGGING_CATEGORY(logBackend, "quickshell.windows.bluetooth", QtWarningMsg);

constexpr unsigned long STOP_TIMEOUT_MS = 3000;
} // namespace

BtBackend::BtBackend(WinBluetooth* frontend): mThread(std::make_unique<QThread>()) {
	this->mThread->setObjectName(QStringLiteral("qs-bluetooth"));
	this->mWorker = new BtWorker(frontend);
	this->mWorker->moveToThread(this->mThread.get());

	QObject::connect(this->mThread.get(), &QThread::started, this->mWorker, &BtWorker::start);
	QObject::connect(this->mThread.get(), &QThread::finished, this->mWorker, &BtWorker::shutdown);

	auto* thread = this->mThread.get();
	qs::windows::startup::afterFirstFrame(thread, [this, thread]() {
		if (this->mWorker != nullptr) thread->start();
	});
}

BtBackend::~BtBackend() { this->stop(); }

void BtBackend::stop() {
	if (this->mWorker == nullptr) return;

	this->mThread->quit();
	if (!this->mThread->wait(STOP_TIMEOUT_MS)) {
		qCWarning(logBackend) << "Bluetooth worker didn't stop in time";
		this->mWorker = nullptr;
		(void) this->mThread.release();
		return;
	}

	delete this->mWorker;
	this->mWorker = nullptr;
}

void BtBackend::setPowered(bool on, quint64 seq) {
	if (auto* worker = this->mWorker) {
		QMetaObject::invokeMethod(worker, [worker, on, seq] { worker->cmdSetPowered(on, seq); }, Qt::QueuedConnection);
	}
}

void BtBackend::setDiscovering(bool on) {
	if (auto* worker = this->mWorker) {
		QMetaObject::invokeMethod(worker, [worker, on] { worker->cmdSetDiscovering(on); }, Qt::QueuedConnection);
	}
}

void BtBackend::pair(const QString& key) {
	if (auto* worker = this->mWorker) {
		QMetaObject::invokeMethod(worker, [worker, key] { worker->cmdPair(key); }, Qt::QueuedConnection);
	}
}

void BtBackend::cancelPair(const QString& key) {
	if (auto* worker = this->mWorker) {
		QMetaObject::invokeMethod(worker, [worker, key] { worker->cmdCancelPair(key); }, Qt::QueuedConnection);
	}
}

void BtBackend::forget(const QString& key) {
	if (auto* worker = this->mWorker) {
		QMetaObject::invokeMethod(worker, [worker, key] { worker->cmdForget(key); }, Qt::QueuedConnection);
	}
}

void BtBackend::connectDevice(const QString& key, bool connect) {
	if (auto* worker = this->mWorker) {
		QMetaObject::invokeMethod(
		    worker,
		    [worker, key, connect] { worker->cmdConnect(key, connect); },
		    Qt::QueuedConnection
		);
	}
}

} // namespace qs::bluetooth
