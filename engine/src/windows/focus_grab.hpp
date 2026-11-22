#pragma once

#include <qt_windows.h>

#include <qlist.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qqmlparserstatus.h>
#include <qquickwindow.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>

namespace qs::windows {

class FocusGrab
    : public QObject
    , public QQmlParserStatus {
	Q_OBJECT;
	QML_ELEMENT;
	Q_INTERFACES(QQmlParserStatus);
	Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged);
	Q_PROPERTY(QList<QObject*> windows READ windows WRITE setWindows NOTIFY windowsChanged);

public:
	explicit FocusGrab(QObject* parent = nullptr): QObject(parent) {}
	~FocusGrab() override;
	Q_DISABLE_COPY_MOVE(FocusGrab);

	void classBegin() override {}
	void componentComplete() override;

	[[nodiscard]] bool isActive() const { return this->grabActive; }
	void setActive(bool active);

	[[nodiscard]] QObjectList windows() const { return this->windowObjects; }
	void setWindows(QObjectList windows);

	[[nodiscard]] bool containsWindow(HWND hwnd) const;
	[[nodiscard]] DWORD activatedAt() const { return this->mActivatedAt; }
	void clear();

signals:
	void cleared();
	void activeChanged();
	void windowsChanged();

private slots:
	void onWindowsChanged();
	void onObjectDestroyed(QObject* object);

private:
	[[nodiscard]] QList<QQuickWindow*> visibleWindows() const;
	void tryActivate();
	void deactivate();
	void refocus();

	bool complete = false;
	bool targetActive = false;
	bool grabActive = false;
	DWORD mActivatedAt = 0;
	QObjectList windowObjects;
	QList<HWND> knownWindows;
};

} // namespace qs::windows
