#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::windows::sys {

class TimeZones: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit TimeZones(QObject* parent = nullptr): QObject(parent) {}

	Q_INVOKABLE [[nodiscard]] QVariant offsetMinutes(const QString& id) const;
	Q_INVOKABLE [[nodiscard]] QStringList ids() const;
};

} // namespace qs::windows::sys
