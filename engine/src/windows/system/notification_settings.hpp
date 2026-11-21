#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

namespace qs::windows::sys {

class NotificationSettings: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit NotificationSettings(QObject* parent = nullptr): QObject(parent) {}

	Q_INVOKABLE static void openSettings();
	Q_INVOKABLE static void openAccessSettings();
};

} // namespace qs::windows::sys
