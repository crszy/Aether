#pragma once

#include <QtCore/qtmetamacros.h>
#include <qcontainerfwd.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

#include "../../../core/doc.hpp"
#include "../../../core/model.hpp"
#include "../../../core/reload.hpp"
#include "notification.hpp"
#include "server.hpp"

namespace qs::windows::services::notifications {

class NotificationServerQml: public PostReloadHook {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(bool keepOnReload READ keepOnReload WRITE setKeepOnReload NOTIFY keepOnReloadChanged);
	Q_PROPERTY(bool persistenceSupported READ persistenceSupported WRITE setPersistenceSupported NOTIFY persistenceSupportedChanged);
	Q_PROPERTY(bool bodySupported READ bodySupported WRITE setBodySupported NOTIFY bodySupportedChanged);
	Q_PROPERTY(bool bodyMarkupSupported READ bodyMarkupSupported WRITE setBodyMarkupSupported NOTIFY bodyMarkupSupportedChanged);
	Q_PROPERTY(bool bodyHyperlinksSupported READ bodyHyperlinksSupported WRITE setBodyHyperlinksSupported NOTIFY bodyHyperlinksSupportedChanged);
	Q_PROPERTY(bool bodyImagesSupported READ bodyImagesSupported WRITE setBodyImagesSupported NOTIFY bodyImagesSupportedChanged);
	Q_PROPERTY(bool actionsSupported READ actionsSupported WRITE setActionsSupported NOTIFY actionsSupportedChanged);
	Q_PROPERTY(bool actionIconsSupported READ actionIconsSupported WRITE setActionIconsSupported NOTIFY actionIconsSupportedChanged);
	Q_PROPERTY(bool imageSupported READ imageSupported WRITE setImageSupported NOTIFY imageSupportedChanged);
	Q_PROPERTY(bool inlineReplySupported READ inlineReplySupported WRITE setInlineReplySupported NOTIFY inlineReplySupportedChanged);
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::windows::services::notifications::Notification>*);
	Q_PROPERTY(UntypedObjectModel* trackedNotifications READ trackedNotifications NOTIFY trackedNotificationsChanged);
	Q_PROPERTY(QVector<QString> extraHints READ extraHints WRITE setExtraHints NOTIFY extraHintsChanged);
	Q_PROPERTY(bool mirrorSystemNotifications READ mirrorSystemNotifications WRITE setMirrorSystemNotifications NOTIFY mirrorSystemNotificationsChanged);
	Q_PROPERTY(qs::windows::services::notifications::SystemNotificationAccess::Enum systemNotificationAccess READ systemNotificationAccess NOTIFY systemNotificationAccessChanged);
	Q_PROPERTY(bool systemMirrorActive READ systemMirrorActive NOTIFY systemMirrorActiveChanged);
	// clang-format on
	QML_NAMED_ELEMENT(NotificationServer);

public:
	void onPostReload() override;

	[[nodiscard]] bool keepOnReload() const;
	void setKeepOnReload(bool keepOnReload);

	[[nodiscard]] bool persistenceSupported() const;
	void setPersistenceSupported(bool persistenceSupported);

	[[nodiscard]] bool bodySupported() const;
	void setBodySupported(bool bodySupported);

	[[nodiscard]] bool bodyMarkupSupported() const;
	void setBodyMarkupSupported(bool bodyMarkupSupported);

	[[nodiscard]] bool bodyHyperlinksSupported() const;
	void setBodyHyperlinksSupported(bool bodyHyperlinksSupported);

	[[nodiscard]] bool bodyImagesSupported() const;
	void setBodyImagesSupported(bool bodyImagesSupported);

	[[nodiscard]] bool actionsSupported() const;
	void setActionsSupported(bool actionsSupported);

	[[nodiscard]] bool actionIconsSupported() const;
	void setActionIconsSupported(bool actionIconsSupported);

	[[nodiscard]] bool imageSupported() const;
	void setImageSupported(bool imageSupported);

	[[nodiscard]] bool inlineReplySupported() const;
	void setInlineReplySupported(bool inlineReplySupported);

	[[nodiscard]] QVector<QString> extraHints() const;
	void setExtraHints(QVector<QString> extraHints);

	[[nodiscard]] ObjectModel<Notification>* trackedNotifications() const;

	[[nodiscard]] bool mirrorSystemNotifications() const;
	void setMirrorSystemNotifications(bool mirror);

	[[nodiscard]] static SystemNotificationAccess::Enum systemNotificationAccess();
	[[nodiscard]] static bool systemMirrorActive();

	Q_INVOKABLE quint32 notifySend(const QString& summary, const QString& body, const QStringList& args = {});

	Q_INVOKABLE void requestSystemNotificationAccess();

signals:
	void notification(qs::windows::services::notifications::Notification* notification);
	void actionInvoked(quint32 id, const QString& action);

	void keepOnReloadChanged();
	void persistenceSupportedChanged();
	void bodySupportedChanged();
	void bodyMarkupSupportedChanged();
	void bodyHyperlinksSupportedChanged();
	void bodyImagesSupportedChanged();
	void actionsSupportedChanged();
	void actionIconsSupportedChanged();
	void imageSupportedChanged();
	void inlineReplySupportedChanged();
	void extraHintsChanged();
	void trackedNotificationsChanged();
	void mirrorSystemNotificationsChanged();
	void systemNotificationAccessChanged();
	void systemMirrorActiveChanged();

private:
	void updateSupported();

	bool live = false;
	bool mKeepOnReload = true;
	bool mMirrorSystemNotifications = true;
	NotificationServerSupport support;
};

} // namespace qs::windows::services::notifications
