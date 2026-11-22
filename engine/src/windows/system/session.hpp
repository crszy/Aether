#pragma once

#include <qobject.h>
#include <qstring.h>
#include <qvariant.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

namespace qs::windows::sys {

class Session: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	Q_PROPERTY(bool canHibernate READ canHibernate CONSTANT);

public:
	explicit Session(QObject* parent = nullptr): QObject(parent) {}

	Q_INVOKABLE static void lock();
	Q_INVOKABLE static void logout();
	Q_INVOKABLE static void shutdown();
	Q_INVOKABLE static void reboot();
	Q_INVOKABLE static void suspend();
	Q_INVOKABLE static void hibernate();
	Q_INVOKABLE static void rebootToFirmware();
	Q_INVOKABLE static void playSystemSound(const QString& name);
	Q_INVOKABLE static QVariantMap osInfo();

	[[nodiscard]] static bool canHibernate();
};

} // namespace qs::windows::sys
