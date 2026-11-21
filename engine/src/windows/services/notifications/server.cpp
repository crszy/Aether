#include "server.hpp"
#include <functional>
#include <utility>

#include <qcontainerfwd.h>
#include <qdesktopservices.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qqmlengine.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qurl.h>

#include "../../../core/desktopentry.hpp"
#include "../../../core/instanceinfo.hpp"
#include "../../../core/logcat.hpp"
#include "../../../core/model.hpp"
#include "../../desktopentry_backend.hpp"
#include "../../window_tracker.hpp"
#include "notification.hpp"
#include "notify_send.hpp"
#include "toast_mirror.hpp"

namespace qs::windows::services::notifications {

// NOLINTNEXTLINE(misc-use-internal-linkage)
QS_LOGGING_CATEGORY(logNotifications, "quickshell.windows.notifications", QtWarningMsg);

namespace {
constexpr auto BUS_CLASS = L"QuickshellNotificationBus";
constexpr auto BUS_MUTEX = L"Local\\quickshell-notification-bus";
constexpr ULONG_PTR NOTIFY_SEND_MAGIC = 0x514e5331;

QString systemUriFor(const QString& aumid) {
	static const QHash<QString, QString> uris {
	    {QStringLiteral("windows.defender.securitycenter"), QStringLiteral("windowsdefender:")},
	};
	return uris.value(aumid.toLower());
}

bool hasWindowFor(const QString& aumid) {
	for (auto* window: qs::windows::WindowTracker::instance()->windows()) {
		if (window->appId().compare(aumid, Qt::CaseInsensitive) == 0) return true;
	}
	return false;
}
} // namespace

NotificationServer* NotificationServer::instance() {
	static auto* instance = [] {
		auto* server = new NotificationServer(); // NOLINT
		server->claimSession();
		return server;
	}();

	return instance;
}

void NotificationServer::claimSession() {
	if (this->mOwner) return;

	auto entry = QFileInfo(InstanceInfo::CURRENT.configPath).fileName();
	if (!entry.isEmpty() && entry.compare("shell.qml", Qt::CaseInsensitive) != 0) return;

	auto* mutex = CreateMutexW(nullptr, FALSE, BUS_MUTEX);
	if (mutex == nullptr) return;

	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		CloseHandle(mutex);

		if (!this->claimRetry.isActive()) {
			qCInfo(logNotifications) << "Another shell owns the notification server; forwarding to it.";
			this->claimRetry.setInterval(5000);
			QObject::connect(&this->claimRetry, &QTimer::timeout, this, &NotificationServer::claimSession);
			this->claimRetry.start();
		}

		return;
	}

	this->claimRetry.stop();

	this->ownerMutex = mutex;
	this->mOwner = true;
	if (this->mMirrorWanted) this->mirror()->setEnabled(true);

	WNDCLASSW wndClass {};
	wndClass.lpfnWndProc = &NotificationServer::busWindowProc;
	wndClass.hInstance = GetModuleHandleW(nullptr);
	wndClass.lpszClassName = BUS_CLASS;
	RegisterClassW(&wndClass);

	this->busWindow = CreateWindowExW(
	    0,
	    BUS_CLASS,
	    L"",
	    0,
	    0,
	    0,
	    0,
	    0,
	    HWND_MESSAGE,
	    nullptr,
	    wndClass.hInstance,
	    nullptr
	);

	if (this->busWindow == nullptr) {
		qCWarning(logNotifications) << "Could not create the notification bus window; other qs "
		                               "processes will show their notifications themselves.";
	}
}

LRESULT CALLBACK
NotificationServer::busWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_COPYDATA) {
		const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam); // NOLINT
		if (data == nullptr || data->dwData != NOTIFY_SEND_MAGIC) return 0;

		auto json = QJsonDocument::fromJson(
		    QByteArray(static_cast<const char*>(data->lpData), static_cast<qsizetype>(data->cbData))
		);

		auto list = json.array().toVariantList();
		if (list.length() < 2) return 0;

		auto summary = list.takeFirst().toString();
		auto body = list.takeFirst().toString();
		QStringList args;
		for (const auto& arg: list) args.append(arg.toString());

		return NotificationServer::instance()->notifySend(summary, body, args);
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

quint32 NotificationServer::forwardToOwner(
    const QString& summary,
    const QString& body,
    const QStringList& args
) {
	auto* owner = FindWindowExW(HWND_MESSAGE, nullptr, BUS_CLASS, nullptr);
	if (owner == nullptr) return 0;

	QJsonArray array {summary, body};
	for (const auto& arg: args) array.append(arg);
	auto payload = QJsonDocument(array).toJson(QJsonDocument::Compact);

	COPYDATASTRUCT data {};
	data.dwData = NOTIFY_SEND_MAGIC;
	data.cbData = static_cast<DWORD>(payload.size());
	data.lpData = payload.data();

	DWORD_PTR result = 0;
	auto ok = SendMessageTimeoutW(
	    owner,
	    WM_COPYDATA,
	    0,
	    reinterpret_cast<LPARAM>(&data),
	    SMTO_ABORTIFHUNG | SMTO_BLOCK,
	    2000,
	    &result
	);

	if (ok == 0 || result == 0) {
		qCInfo(logNotifications) << "Forwarding a notification to the shell failed; showing it here.";
		return 0;
	}

	return static_cast<quint32>(result);
}

void NotificationServer::switchGeneration(bool reEmit, const std::function<void()>& clearHook) {
	auto notifications = this->mNotifications.valueList();
	this->mNotifications.valueList().clear();
	this->idMap.clear();
	this->toastMap.clear();

	clearHook();

	if (!this->pendingSends.isEmpty()) {
		QMetaObject::invokeMethod(
		    this,
		    [sends = std::exchange(this->pendingSends, {})] {
			    for (const auto& send: sends) send();
		    },
		    Qt::QueuedConnection
		);
	}

	if (reEmit) {
		for (auto* notification: notifications) {
			notification->setLastGeneration();
			notification->setTracked(false);
			emit this->notification(notification);

			if (!notification->isTracked()) {
				delete notification;
			} else {
				this->idMap.insert(notification->id(), notification);
				if (notification->toastId() != 0) this->toastMap.insert(notification->toastId(), notification);
				this->mNotifications.insertObject(notification);
			}
		}
	} else {
		for (auto* notification: notifications) {
			delete notification;
		}
	}
}

ObjectModel<Notification>* NotificationServer::trackedNotifications() {
	return &this->mNotifications;
}

void NotificationServer::deleteNotification(
    Notification* notification,
    NotificationCloseReason::Enum reason
) {
	if (!this->idMap.contains(notification->id())) return;

	emit notification->closed(reason);

	this->mNotifications.removeObject(notification);
	this->idMap.remove(notification->id());

	if (auto toastId = notification->toastId(); toastId != 0) {
		this->toastMap.remove(toastId);

		if (reason == NotificationCloseReason::Dismissed && this->mMirror) {
			this->mMirror->removeToast(toastId);
		}
	}

	notification->retainedDestroy();
}

quint32 NotificationServer::notify(
    const QString& appName,
    quint32 replacesId,
    const QString& appIcon,
    const QString& summary,
    const QString& body,
    const QStringList& actions,
    const QVariantMap& hints,
    qint32 expireTimeout
) {
	return this->deliver(0, replacesId, appName, appIcon, summary, body, actions, hints, expireTimeout);
}

quint32 NotificationServer::notifySend(
    const QString& summary,
    const QString& body,
    const QStringList& args
) {
	if (!this->mOwner) {
		if (auto id = this->forwardToOwner(summary, body, args)) return id;
	}

	auto request = parseNotifySendArgs(args);

	auto replacing = request.replacesId != 0 && this->idMap.contains(request.replacesId);
	auto reservedId = replacing ? 0 : this->nextId++;

	auto send = [this, reservedId, request, summary, body] {
		this->deliver(
		    reservedId,
		    request.replacesId,
		    request.appName,
		    request.appIcon,
		    summary,
		    body,
		    request.actions,
		    request.hints,
		    request.expireTimeout
		);
	};

	QMetaObject::invokeMethod(
	    this,
	    [this, send] {
		    if (this->hasReceiver()) send();
		    else this->pendingSends.append(send);
	    },
	    Qt::QueuedConnection
	);

	return replacing ? request.replacesId : reservedId;
}

bool NotificationServer::hasReceiver() const {
	static const auto signal = QMetaMethod::fromSignal(&NotificationServer::notification);
	return this->isSignalConnected(signal);
}

quint32 NotificationServer::deliver(
    quint32 reservedId,
    quint32 replacesId,
    const QString& appName,
    const QString& appIcon,
    const QString& summary,
    const QString& body,
    const QStringList& actions,
    const QVariantMap& hints,
    qint32 expireTimeout,
    quint32 toastId,
    const QString& aumid
) {
	auto* notification = replacesId == 0 ? nullptr : this->idMap.value(replacesId);
	auto old = notification != nullptr;

	if (!notification) {
		notification = new Notification(reservedId != 0 ? reservedId : this->nextId++, this);
		QQmlEngine::setObjectOwnership(notification, QQmlEngine::CppOwnership);
		if (toastId != 0) notification->setSystemToast(toastId, aumid);
	}

	notification->updateProperties(appName, appIcon, summary, body, actions, hints, expireTimeout);

	if (!old) {
		emit this->notification(notification);

		if (!notification->isTracked()) {
			auto id = notification->id();
			delete notification;
			return id;
		}

		this->idMap.insert(notification->id(), notification);
		if (toastId != 0) this->toastMap.insert(toastId, notification);
		this->mNotifications.insertObject(notification);
	}

	return notification->id();
}

void NotificationServer::invokeAction(Notification* notification, const QString& identifier) {
	if (notification->toastId() != 0 && identifier == QStringLiteral("default")) {
		NotificationServer::activateApp(
		    notification->aumid(),
		    notification->bindableDesktopEntry().value()
		);
	}

	emit this->actionInvoked(notification->id(), identifier);
}

void NotificationServer::activateApp(const QString& aumid, const QString& desktopEntry) {
	if (aumid.isEmpty()) return;

	for (auto* window: qs::windows::WindowTracker::instance()->windows()) {
		if (window->appId().compare(aumid, Qt::CaseInsensitive) == 0) {
			window->activate();
			return;
		}
	}

	auto token = desktopEntry.isEmpty()
	               ? QString()
	               : qs::windows::WindowsDesktopEntryBackend::parsingNameForId(desktopEntry);

	if (token.isEmpty()
	    && !qs::windows::WindowsDesktopEntryBackend::idForParsingName(aumid).isEmpty())
	{
		token = aumid;
	}

	if (token.isEmpty()) {
		auto uri = systemUriFor(aumid);
		if (!uri.isEmpty()) {
			QDesktopServices::openUrl(QUrl(uri));
		} else {
			qCDebug(logNotifications) << "No app to open for a notification from" << aumid;
		}
		return;
	}

	qs::windows::WindowsDesktopEntryBackend::launch(token);
}

ToastMirror* NotificationServer::mirror() {
	if (this->mMirror) return this->mMirror;

	DesktopEntryManager::instance();

	this->mMirror = new ToastMirror(this);

	QObject::connect(this->mMirror, &ToastMirror::toastAdded, this, &NotificationServer::onToastAdded);
	QObject::connect(this->mMirror, &ToastMirror::toastRemoved, this, &NotificationServer::onToastRemoved);

	QObject::connect(
	    this->mMirror,
	    &ToastMirror::accessChanged,
	    this,
	    &NotificationServer::systemAccessChanged
	);

	QObject::connect(
	    this->mMirror,
	    &ToastMirror::activeChanged,
	    this,
	    &NotificationServer::systemMirrorActiveChanged
	);

	return this->mMirror;
}

void NotificationServer::setMirrorEnabled(bool enabled) {
	this->mMirrorWanted = enabled;
	if (!this->mOwner) enabled = false;
	if (!enabled && !this->mMirror) return;
	this->mirror()->setEnabled(enabled);
}

void NotificationServer::requestSystemAccess() { this->mirror()->requestAccess(); }

SystemNotificationAccess::Enum NotificationServer::systemAccess() const {
	return this->mMirror ? this->mMirror->access() : SystemNotificationAccess::Unknown;
}

bool NotificationServer::isSystemMirrorActive() const {
	return this->mMirror && this->mMirror->isActive();
}

void NotificationServer::onToastAdded(const ToastSnapshot& toast) {
	if (this->toastMap.contains(toast.id)) return;

	auto hints = QVariantMap();
	hints.insert("x-windows-aumid", toast.aumid);
	hints.insert("x-windows-toast-id", toast.id);

	auto desktopEntry = qs::windows::WindowsDesktopEntryBackend::idForParsingName(toast.aumid);
	if (!desktopEntry.isEmpty()) hints.insert("desktop-entry", desktopEntry);

	qCDebug(logNotifications) << "Mirroring toast" << toast.id << "from" << toast.aumid
	                          << "entry" << desktopEntry;

	auto actions = QStringList();
	auto canOpen = !desktopEntry.isEmpty() || !systemUriFor(toast.aumid).isEmpty();
	if (canOpen || hasWindowFor(toast.aumid)) {
		actions = {QStringLiteral("default"), QStringLiteral("Open")};
	}

	this->deliver(
	    0,
	    0,
	    toast.appName,
	    toast.logoPath,
	    toast.summary,
	    toast.body.toHtmlEscaped(),
	    actions,
	    hints,
	    -1,
	    toast.id,
	    toast.aumid
	);
}

void NotificationServer::onToastRemoved(quint32 toastId) {
	if (auto* notification = this->toastMap.value(toastId)) {
		this->deleteNotification(notification, NotificationCloseReason::CloseRequested);
	}
}

} // namespace qs::windows::services::notifications
