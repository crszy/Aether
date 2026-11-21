#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::windows::sys {

class TerminalColors: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit TerminalColors(QObject* parent = nullptr): QObject(parent) {}

	Q_INVOKABLE static QVariantMap generate(
	    const QVariantMap& baseScheme,
	    const QString& primaryKeyColor,
	    const QString& surfaceContainerLow,
	    const QString& onSurface,
	    bool darkMode,
	    double harmony,
	    double harmonizeThreshold,
	    double fgBoost,
	    bool monochrome
	);

	Q_INVOKABLE static double hue(const QString& color);
};

} // namespace qs::windows::sys
