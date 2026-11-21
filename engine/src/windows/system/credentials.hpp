#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>

namespace qs::windows::sys {

class Credentials: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Credentials(QObject* parent = nullptr): QObject(parent) {}

	Q_INVOKABLE static bool write(const QString& target, const QString& secret);
	Q_INVOKABLE static QString read(const QString& target);
	Q_INVOKABLE static bool remove(const QString& target);
	Q_INVOKABLE static bool exists(const QString& target);
};

} // namespace qs::windows::sys
