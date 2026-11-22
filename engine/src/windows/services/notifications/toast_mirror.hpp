#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::windows::services::notifications {

class SystemNotificationAccess: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Unknown = 0,
		Unspecified = 1,
		Allowed = 2,
		Denied = 3,
		Unavailable = 4,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString
	toString(qs::windows::services::notifications::SystemNotificationAccess::Enum value);
};

struct ToastSnapshot {
	quint32 id = 0;
	QString aumid;
	QString appName;
	QString logoPath;
	QString summary;
	QString body;
};

class ToastMirrorWorker;

class ToastMirror: public QObject {
	Q_OBJECT;

public:
	explicit ToastMirror(QObject* parent = nullptr);
	~ToastMirror() override;
	Q_DISABLE_COPY_MOVE(ToastMirror);

	void setEnabled(bool enabled);
	void requestAccess();
	void removeToast(quint32 id);

	[[nodiscard]] SystemNotificationAccess::Enum access() const { return this->mAccess; }
	[[nodiscard]] bool isActive() const { return this->mActive; }

signals:
	void toastAdded(const qs::windows::services::notifications::ToastSnapshot& toast);
	void toastRemoved(quint32 id);
	void accessChanged();
	void activeChanged();

private:
	friend class ToastMirrorWorker;

	void workerState(SystemNotificationAccess::Enum access, bool active);

	QThread mThread;
	ToastMirrorWorker* mWorker = nullptr;
	SystemNotificationAccess::Enum mAccess = SystemNotificationAccess::Unknown;
	bool mActive = false;
};

} // namespace qs::windows::services::notifications
