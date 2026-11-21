#pragma once

#include <qcontainerfwd.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtypes.h>
#include <qvariant.h>

namespace qs::windows::services::notifications {

struct NotifySendRequest {
	QString appName = QStringLiteral("notify-send");
	QString appIcon;
	quint32 replacesId = 0;
	QStringList actions;
	QVariantMap hints;
	qint32 expireTimeout = -1;
};

NotifySendRequest parseNotifySendArgs(const QStringList& args);

} // namespace qs::windows::services::notifications
