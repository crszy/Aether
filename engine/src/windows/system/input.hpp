#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

namespace qs::windows::sys {

class Input: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Input(QObject* parent = nullptr): QObject(parent) {}

	Q_INVOKABLE static void sendKey(int keycode, bool down);
	Q_INVOKABLE static void sendText(const QString& text);
	Q_INVOKABLE static QVariantMap cursorPosition();
};

} // namespace qs::windows::sys
