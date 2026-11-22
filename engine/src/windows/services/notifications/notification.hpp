#pragma once

#include <utility>

#include <qcontainerfwd.h>
#include <qlist.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../../core/retainable.hpp"

namespace qs::windows::services::notifications {

class NotificationUrgency: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Low = 0,
		Normal = 1,
		Critical = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString
	toString(qs::windows::services::notifications::NotificationUrgency::Enum value);
};

class NotificationCloseReason: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Expired = 1,
		Dismissed = 2,
		CloseRequested = 3,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString
	toString(qs::windows::services::notifications::NotificationCloseReason::Enum value);
};

class NotificationAction;

class Notification
    : public QObject
    , public Retainable {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(quint32 id READ id CONSTANT);
	Q_PROPERTY(bool tracked READ isTracked WRITE setTracked NOTIFY trackedChanged);
	Q_PROPERTY(bool lastGeneration READ isLastGeneration CONSTANT);
	Q_PROPERTY(qreal expireTimeout READ default NOTIFY expireTimeoutChanged BINDABLE bindableExpireTimeout);
	Q_PROPERTY(QString appName READ default NOTIFY appNameChanged BINDABLE bindableAppName);
	Q_PROPERTY(QString appIcon READ default NOTIFY appIconChanged BINDABLE bindableAppIcon);
	Q_PROPERTY(QString summary READ default NOTIFY summaryChanged BINDABLE bindableSummary);
	Q_PROPERTY(QString body READ default NOTIFY bodyChanged BINDABLE bindableBody);
	Q_PROPERTY(qs::windows::services::notifications::NotificationUrgency::Enum urgency READ default NOTIFY urgencyChanged BINDABLE bindableUrgency);
	Q_PROPERTY(QList<qs::windows::services::notifications::NotificationAction*> actions READ actions NOTIFY actionsChanged);
	Q_PROPERTY(bool hasActionIcons READ default NOTIFY hasActionIconsChanged BINDABLE bindableHasActionIcons);
	Q_PROPERTY(bool resident READ default NOTIFY residentChanged BINDABLE bindableResident);
	Q_PROPERTY(bool transient READ default NOTIFY transientChanged BINDABLE bindableTransient);
	Q_PROPERTY(QString desktopEntry READ default NOTIFY desktopEntryChanged BINDABLE bindableDesktopEntry);
	Q_PROPERTY(QString image READ default NOTIFY imageChanged BINDABLE bindableImage);
	Q_PROPERTY(bool hasInlineReply READ default NOTIFY hasInlineReplyChanged BINDABLE bindableHasInlineReply);
	Q_PROPERTY(QString inlineReplyPlaceholder READ default NOTIFY inlineReplyPlaceholderChanged BINDABLE bindableInlineReplyPlaceholder);
	Q_PROPERTY(QVariantMap hints READ default NOTIFY hintsChanged BINDABLE bindableHints);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("Notifications must be acquired from a NotificationServer");

public:
	explicit Notification(quint32 id, QObject* parent): QObject(parent), mId(id) {}

	Q_INVOKABLE void expire();
	Q_INVOKABLE void dismiss();

	Q_INVOKABLE void sendInlineReply(const QString& replyText);

	void updateProperties(
	    const QString& appName,
	    QString appIcon,
	    const QString& summary,
	    const QString& body,
	    const QStringList& actions,
	    QVariantMap hints,
	    qint32 expireTimeout
	);

	void close(NotificationCloseReason::Enum reason);

	[[nodiscard]] quint32 id() const;
	[[nodiscard]] bool isTracked() const;

	[[nodiscard]] bool isLastGeneration() const;
	void setLastGeneration();

	void setSystemToast(quint32 toastId, QString aumid) {
		this->mToastId = toastId;
		this->mAumid = std::move(aumid);
	}
	[[nodiscard]] quint32 toastId() const { return this->mToastId; }
	[[nodiscard]] QString aumid() const { return this->mAumid; }

	[[nodiscard]] QBindable<qreal> bindableExpireTimeout() const { return &this->bExpireTimeout; }
	[[nodiscard]] QBindable<QString> bindableAppName() const { return &this->bAppName; }
	[[nodiscard]] QBindable<QString> bindableAppIcon() const { return &this->bAppIcon; }
	[[nodiscard]] QBindable<QString> bindableSummary() const { return &this->bSummary; }
	[[nodiscard]] QBindable<QString> bindableBody() const { return &this->bBody; }
	[[nodiscard]] QBindable<NotificationUrgency::Enum> bindableUrgency() const {
		return &this->bUrgency;
	}

	[[nodiscard]] QList<NotificationAction*> actions() const;

	[[nodiscard]] QBindable<bool> bindableHasActionIcons() const { return &this->bHasActionIcons; }
	[[nodiscard]] QBindable<bool> bindableResident() const { return &this->bResident; }
	[[nodiscard]] QBindable<bool> bindableTransient() const { return &this->bTransient; }
	[[nodiscard]] QBindable<QString> bindableDesktopEntry() const { return &this->bDesktopEntry; }
	[[nodiscard]] QBindable<QString> bindableImage() const { return &this->bImage; }
	[[nodiscard]] QBindable<bool> bindableHasInlineReply() const { return &this->bHasInlineReply; }

	[[nodiscard]] QBindable<QString> bindableInlineReplyPlaceholder() const {
		return &this->bInlineReplyPlaceholder;
	}

	[[nodiscard]] QBindable<QVariantMap> bindableHints() const { return &this->bHints; }

	[[nodiscard]] NotificationCloseReason::Enum closeReason() const;
	void setTracked(bool tracked);

signals:
	void closed(qs::windows::services::notifications::NotificationCloseReason::Enum reason);

	void trackedChanged();
	void expireTimeoutChanged();
	void appNameChanged();
	void appIconChanged();
	void summaryChanged();
	void bodyChanged();
	void urgencyChanged();
	void actionsChanged();
	void hasActionIconsChanged();
	void residentChanged();
	void transientChanged();
	void desktopEntryChanged();
	void imageChanged();
	void hasInlineReplyChanged();
	void inlineReplyPlaceholderChanged();
	void hintsChanged();

private:
	quint32 mId;
	NotificationCloseReason::Enum mCloseReason = NotificationCloseReason::Dismissed;
	bool mLastGeneration = false;
	QList<NotificationAction*> mActions;
	quint32 mToastId = 0;
	QString mAumid;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(Notification, qreal, bExpireTimeout, &Notification::expireTimeoutChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Notification, QString, bAppName, &Notification::appNameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Notification, QString, bAppIcon, &Notification::appIconChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Notification, QString, bSummary, &Notification::summaryChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Notification, QString, bBody, &Notification::bodyChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Notification, bool, bHasActionIcons, &Notification::hasActionIconsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Notification, bool, bResident, &Notification::residentChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Notification, bool, bTransient, &Notification::transientChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Notification, QString, bDesktopEntry, &Notification::desktopEntryChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Notification, QString, bImage, &Notification::imageChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Notification, bool, bHasInlineReply, &Notification::hasInlineReplyChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Notification, QString, bInlineReplyPlaceholder, &Notification::inlineReplyPlaceholderChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Notification, QVariantMap, bHints, &Notification::hintsChanged);
	// clang-format on

	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(
	    Notification,
	    NotificationUrgency::Enum,
	    bUrgency,
	    NotificationUrgency::Normal,
	    &Notification::urgencyChanged
	);
};

class NotificationAction: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString identifier READ identifier CONSTANT);
	Q_PROPERTY(QString text READ text NOTIFY textChanged);
	QML_ELEMENT;
	QML_UNCREATABLE("NotificationActions must be acquired from a Notification");

public:
	explicit NotificationAction(QString identifier, QString text, Notification* notification)
	    : QObject(notification)
	    , notification(notification)
	    , mIdentifier(std::move(identifier))
	    , mText(std::move(text)) {}

	Q_INVOKABLE void invoke();

	[[nodiscard]] QString identifier() const;
	[[nodiscard]] QString text() const;
	void setText(const QString& text);

signals:
	void textChanged();

private:
	Notification* notification;
	QString mIdentifier;
	QString mText;
};

} // namespace qs::windows::services::notifications
