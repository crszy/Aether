#pragma once

#include <functional>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

class QQuickWindow;

namespace qs::windows::startup {

void afterFirstFrame(QObject* context, std::function<void()> callback);
void watchWindow(QQuickWindow* window);
[[nodiscard]] bool settled();

} // namespace qs::windows::startup

namespace qs::windows {

class StartupGate: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	Q_PROPERTY(bool settled READ settled NOTIFY settledChanged);

public:
	explicit StartupGate(QObject* parent = nullptr);

	[[nodiscard]] bool settled() const { return qs::windows::startup::settled(); }

signals:
	void settledChanged();
};

} // namespace qs::windows
