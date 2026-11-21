#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::windows::sys {

class NightLight: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit NightLight(QObject* parent = nullptr): QObject(parent) {}

	Q_INVOKABLE void enable(int colorTemperatureKelvin);
	Q_INVOKABLE void disable();

	Q_INVOKABLE void setDim(const QString& screenName, qreal factor);
	Q_INVOKABLE qreal probeDimFloor(const QString& screenName);
};

} // namespace qs::windows::sys
