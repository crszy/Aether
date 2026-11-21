#pragma once

#include <vector>

#include <qt_windows.h>

#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qpoint.h>
#include <qproperty.h>
#include <qrect.h>
#include <qscreen.h>
#include <qset.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::windows {

class WindowTracker;
class VirtualDesktops;

struct WindowEvent {
	DWORD event = 0;
	HWND hwnd = nullptr;
	DWORD time = 0;
};

class TrackedWindow: public QObject {
	Q_OBJECT;

public:
	[[nodiscard]] HWND hwnd() const { return this->mHwnd; }
	[[nodiscard]] quint64 address() const;
	[[nodiscard]] QString addressHex() const;
	[[nodiscard]] TrackedWindow* owner() const;

	// clang-format off
	[[nodiscard]] QBindable<QString> bindableTitle() { return &this->bTitle; }
	[[nodiscard]] QBindable<QString> bindableAppId() { return &this->bAppId; }
	[[nodiscard]] QBindable<QString> bindableExePath() { return &this->bExePath; }
	[[nodiscard]] QBindable<quint32> bindablePid() { return &this->bPid; }
	[[nodiscard]] QBindable<QScreen*> bindableScreen() { return &this->bScreen; }
	[[nodiscard]] QBindable<QRect> bindableRect() { return &this->bRect; }
	[[nodiscard]] QBindable<bool> bindableMinimized() { return &this->bMinimized; }
	[[nodiscard]] QBindable<bool> bindableMaximized() { return &this->bMaximized; }
	[[nodiscard]] QBindable<bool> bindableFullscreen() { return &this->bFullscreen; }
	[[nodiscard]] QBindable<bool> bindableActivated() { return &this->bActivated; }
	[[nodiscard]] QBindable<qint32> bindableDesktop() { return &this->bDesktop; }
	// clang-format on

	[[nodiscard]] QString title() const { return this->bTitle.value(); }
	[[nodiscard]] QString appId() const { return this->bAppId.value(); }
	[[nodiscard]] QString exePath() const { return this->bExePath.value(); }
	[[nodiscard]] quint32 pid() const { return this->bPid.value(); }
	[[nodiscard]] QScreen* screen() const { return this->bScreen.value(); }
	[[nodiscard]] QRect rect() const { return this->bRect.value(); }
	[[nodiscard]] bool minimized() const { return this->bMinimized.value(); }
	[[nodiscard]] bool maximized() const { return this->bMaximized.value(); }
	[[nodiscard]] bool fullscreen() const { return this->bFullscreen.value(); }
	[[nodiscard]] bool activated() const { return this->bActivated.value(); }
	[[nodiscard]] qint32 desktop() const { return this->bDesktop.value(); }
	[[nodiscard]] bool isUwpFrame() const { return this->uwpFrame; }

	void activate();
	void close();
	void setMinimized(bool minimized);
	void setMaximized(bool maximized);
	void setFullscreen(bool fullscreen);
	void fullscreenOn(QScreen* screen);
	bool moveToDesktop(qsizetype index);
	void moveTo(const QPoint& logical);

signals:
	void closed();
	void titleChanged();
	void appIdChanged();
	void exePathChanged();
	void pidChanged();
	void screenChanged();
	void rectChanged();
	void minimizedChanged();
	void maximizedChanged();
	void fullscreenChanged();
	void activatedChanged();
	void desktopChanged();

public:
	void refreshDesktop(bool askShell);

private:
	friend class WindowTracker;

	void forgetScreen(QScreen* screen);

	explicit TrackedWindow(WindowTracker* tracker, HWND hwnd);

	void refreshIdentity();
	void refreshTitle();
	void refreshState();

	WindowTracker* tracker;
	HWND mHwnd;
	bool uwpFrame = false;

	struct {
		bool active = false;
		LONG_PTR style = 0;
		RECT rect {};
	} fullscreenBackup;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(TrackedWindow, QString, bTitle, &TrackedWindow::titleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(TrackedWindow, QString, bAppId, &TrackedWindow::appIdChanged);
	Q_OBJECT_BINDABLE_PROPERTY(TrackedWindow, QString, bExePath, &TrackedWindow::exePathChanged);
	Q_OBJECT_BINDABLE_PROPERTY(TrackedWindow, quint32, bPid, &TrackedWindow::pidChanged);
	Q_OBJECT_BINDABLE_PROPERTY(TrackedWindow, QScreen*, bScreen, &TrackedWindow::screenChanged);
	Q_OBJECT_BINDABLE_PROPERTY(TrackedWindow, QRect, bRect, &TrackedWindow::rectChanged);
	Q_OBJECT_BINDABLE_PROPERTY(TrackedWindow, bool, bMinimized, &TrackedWindow::minimizedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(TrackedWindow, bool, bMaximized, &TrackedWindow::maximizedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(TrackedWindow, bool, bFullscreen, &TrackedWindow::fullscreenChanged);
	Q_OBJECT_BINDABLE_PROPERTY(TrackedWindow, bool, bActivated, &TrackedWindow::activatedChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(TrackedWindow, qint32, bDesktop, -1, &TrackedWindow::desktopChanged);
	// clang-format on
};

class WindowTracker: public QObject {
	Q_OBJECT;

public:
	static WindowTracker* instance();

	void drainEvents();

	[[nodiscard]] const QList<TrackedWindow*>& windows() const { return this->mWindows; }
	[[nodiscard]] TrackedWindow* activeWindow() const { return this->mActive; }
	[[nodiscard]] TrackedWindow* windowFor(HWND hwnd) const { return this->byHwnd.value(hwnd); }
	[[nodiscard]] VirtualDesktops* desktops() const { return this->mDesktops; }

	[[nodiscard]] QScreen* screenFor(HMONITOR monitor) const;
	[[nodiscard]] static qsizetype screenIndex(QScreen* screen);

	void rescan();
	void refreshForeground();
	void noteMoveSize(HWND hwnd, bool started);

signals:
	void windowAdded(TrackedWindow* window);
	void windowRemoved(TrackedWindow* window);
	void activeWindowChanged();
	void flushed();
	void moveSizeStarted(TrackedWindow* window);
	void moveSizeEnded(TrackedWindow* window);
	void foregroundChanged(HWND hwnd, quint32 time);

private:
	explicit WindowTracker();
	~WindowTracker() override;
	Q_DISABLE_COPY_MOVE(WindowTracker);

	struct Dirty {
		bool eligibility = false;
		bool title = false;
		bool state = false;
		bool desktop = false;
		bool askShell = false;
	};

	void startEventThread();
	void onEvent(DWORD event, HWND hwnd, DWORD time);
	void schedule();
	void flush();

	bool isEligible(HWND hwnd) const;
	void addWindow(HWND hwnd);
	void removeWindow(TrackedWindow* window);
	bool sweepDestroyed();
	void updateActive();
	void setActive(TrackedWindow* window);
	void updateScreens();
	void onScreensChanged();
	void onDesktopsChanged();

	QList<TrackedWindow*> mWindows;
	QHash<HWND, TrackedWindow*> byHwnd;
	TrackedWindow* mActive = nullptr;
	VirtualDesktops* mDesktops = nullptr;

	QHash<HWND, Dirty> dirty;
	QSet<HWND> candidates;
	QSet<HWND> moving;
	bool foregroundDirty = false;
	QTimer flushTimer;
	QTimer sweepTimer;

	QHash<HMONITOR, QScreen*> screensByMonitor;
	std::vector<WindowEvent> spareEvents;
};

} // namespace qs::windows
