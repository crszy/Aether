#pragma once

#include <windows.h>

#include <functional>

#include <qcontainerfwd.h>
#include <qhash.h>
#include <qobject.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../../core/model.hpp"
#include "notification.hpp"
#include "toast_mirror.hpp"

namespace qs::windows::services::notifications {

struct NotificationServerSupport {
	bool persistence = false;
	bool body = true;
	bool bodyMarkup = false;
	bool bodyHyperlinks = false;
	bool bodyImages = false;
	bool actions = false;
	bool actionIcons = false;
	bool image = false;
	bool inlineReply = false;
	QVector<QString> extraHints;
};

class NotificationServer: public QObject {
	Q_OBJECT;

public:
	static NotificationServer* instance();

	void switchGeneration(bool reEmit, const std::function<void()>& clearHook);
	ObjectModel<Notification>* trackedNotifications();
	void deleteNotification(Notification* notification, NotificationCloseReason::Enum reason);

	quint32 notify(
	    const QString& appName,
	    quint32 replacesId,
	    const QString& appIcon,
	    const QString& summary,
	    const QString& body,
	    const QStringList& actions,
	    const QVariantMap& hints,
	    qint32 expireTimeout
	);

	quint32 notifySend(const QString& summary, const QString& body, const QStringList& args);

	void invokeAction(Notification* notification, const QString& identifier);

	void setMirrorEnabled(bool enabled);
	void requestSystemAccess();
	[[nodiscard]] SystemNotificationAccess::Enum systemAccess() const;
	[[nodiscard]] bool isSystemMirrorActive() const;

	NotificationServerSupport support;

signals:
	void notification(qs::windows::services::notifications::Notification* notification);
	void actionInvoked(quint32 id, const QString& action);
	void systemAccessChanged();
	void systemMirrorActiveChanged();

private:
	explicit NotificationServer() = default;

	quint32 deliver(
	    quint32 reservedId,
	    quint32 replacesId,
	    const QString& appName,
	    const QString& appIcon,
	    const QString& summary,
	    const QString& body,
	    const QStringList& actions,
	    const QVariantMap& hints,
	    qint32 expireTimeout,
	    quint32 toastId = 0,
	    const QString& aumid = QString()
	);

	[[nodiscard]] bool hasReceiver() const;

	void claimSession();
	quint32 forwardToOwner(const QString& summary, const QString& body, const QStringList& args);
	static LRESULT CALLBACK busWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	bool mOwner = false;
	bool mMirrorWanted = false;
	QTimer claimRetry;
	void* ownerMutex = nullptr;
	HWND busWindow = nullptr;

	ToastMirror* mirror();
	void onToastAdded(const ToastSnapshot& toast);
	void onToastRemoved(quint32 toastId);
	static void activateApp(const QString& aumid, const QString& desktopEntry);

	quint32 nextId = 1;
	QHash<quint32, Notification*> idMap;
	QHash<quint32, Notification*> toastMap;
	ObjectModel<Notification> mNotifications {this};
	ToastMirror* mMirror = nullptr;
	QList<std::function<void()>> pendingSends;
};

} // namespace qs::windows::services::notifications
