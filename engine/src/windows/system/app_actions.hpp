#pragma once

#include <qhash.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::windows::sys {

struct AppActionInfo {
	QString token;
	QString name;
	QString target;
	QString packageFamilyName;
	bool packaged = false;
	bool canRunAsAdmin = false;
	bool canOpenLocation = false;
	bool canUninstall = false;
};

class AppActions: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit AppActions(QObject* parent = nullptr);

	Q_INVOKABLE QVariantMap info(const QString& entryId);
	Q_INVOKABLE bool runAsAdmin(const QString& entryId);
	Q_INVOKABLE bool openFileLocation(const QString& entryId);
	Q_INVOKABLE bool openAppSettings(const QString& entryId);
	Q_INVOKABLE bool uninstall(const QString& entryId);

private:
	AppActionInfo details(const QString& entryId);

	QHash<QString, AppActionInfo> mCache;
};

} // namespace qs::windows::sys
