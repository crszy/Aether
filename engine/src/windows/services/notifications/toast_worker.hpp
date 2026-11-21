#pragma once

#include <qhash.h>
#include <qobject.h>
#include <qset.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.UI.Notifications.h>

#include "toast_mirror.hpp"

class QTimer;

namespace qs::windows::services::notifications {

class ToastMirrorWorker: public QObject {
	Q_OBJECT;

public:
	explicit ToastMirrorWorker(ToastMirror* frontend);
	~ToastMirrorWorker() override;
	Q_DISABLE_COPY_MOVE(ToastMirrorWorker);

	void setEnabled(bool enabled);
	void requestAccess();
	void removeToast(quint32 id);

public slots:
	void start();
	void shutdown();

private:
	void begin(bool request);
	void stop();
	void applyAccess(winrt::Windows::UI::Notifications::Management::UserNotificationListenerAccessStatus status);
	void checkAccess();
	void resync(bool announce);
	void report();

	[[nodiscard]] ToastSnapshot
	snapshotOf(const winrt::Windows::UI::Notifications::UserNotification& toast);
	QString logoFor(const winrt::Windows::ApplicationModel::AppInfo& info, const QString& aumid);

	ToastMirror* mFrontend;
	bool mApartment = false;
	bool mEnabled = false;

	winrt::Windows::UI::Notifications::Management::UserNotificationListener mListener {nullptr};
	winrt::event_token mChangedToken {};
	bool mEvents = false;

	QTimer* mPollTimer = nullptr;
	QTimer* mAccessTimer = nullptr;
	QTimer* mEventTimer = nullptr;

	bool mBaselined = false;
	QSet<quint32> mKnown;
	QHash<QString, QString> mLogos;

	SystemNotificationAccess::Enum mAccess = SystemNotificationAccess::Unknown;
	bool mActive = false;
};

} // namespace qs::windows::services::notifications
