#include "window_manager.hpp"
#include <cwchar>

#include <qcolor.h>
#include <qcoreapplication.h>
#include <qdatetime.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpointer.h>
#include <qstring.h>
#include <qtimer.h>

#include "util.hpp"
#include "virtual_desktops.hpp"
#include "window_decorations.hpp"
#include "window_tracker.hpp"

#include <dwmapi.h>

namespace qs::windows {

namespace {

Q_LOGGING_CATEGORY(logWorkspace, "quickshell.windows.workspace", QtWarningMsg);

constexpr qint64 WARP_TIMEOUT_MS = 2000;
constexpr int WARP_FALLBACK_MS = 500;
constexpr int FOCUS_REPAIR_MS = 150;

bool isNullGuid(const GUID& id) { return IsEqualGUID(id, GUID {}) != FALSE; }

bool isCloaked(HWND hwnd) {
	DWORD cloaked = 0;
	return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
	    && cloaked != 0;
}

bool isDesktopWindow(HWND hwnd) {
	if (hwnd == GetShellWindow()) return true;
	wchar_t name[16] {};
	GetClassNameW(hwnd, name, 16);
	return wcscmp(name, L"WorkerW") == 0 || wcscmp(name, L"Progman") == 0;
}

} // namespace

WorkspaceBehavior* WorkspaceBehavior::instance() {
	static QPointer<WorkspaceBehavior> behavior; // NOLINT

	if (behavior.isNull()) {
		behavior = new WorkspaceBehavior(QCoreApplication::instance());
	}

	return behavior.data();
}

WorkspaceBehavior::WorkspaceBehavior(QObject* parent)
    : QObject(parent)
    , desktops(VirtualDesktops::instance()) {
	this->currentIndex = this->desktops->currentIndex();
	const auto& list = this->desktops->desktops();
	if (this->currentIndex >= 0 && this->currentIndex < list.length()) {
		this->currentId = list[this->currentIndex].id;
	}

	QObject::connect(
	    this->desktops,
	    &VirtualDesktops::currentChanged,
	    this,
	    &WorkspaceBehavior::onCurrentChanged
	);

	this->fallbackTimer.setSingleShot(true);
	this->fallbackTimer.setInterval(WARP_FALLBACK_MS);
	QObject::connect(&this->fallbackTimer, &QTimer::timeout, this, &WorkspaceBehavior::warpToTopmost);

	this->focusTimer.setSingleShot(true);
	this->focusTimer.setInterval(FOCUS_REPAIR_MS);
	QObject::connect(&this->focusTimer, &QTimer::timeout, this, &WorkspaceBehavior::repairFocus);
}

void WorkspaceBehavior::setBackAndForth(bool enabled) {
	if (enabled == this->mBackAndForth) return;
	this->mBackAndForth = enabled;
	emit this->backAndForthChanged();
}

void WorkspaceBehavior::setWarpOnChange(qint32 mode) {
	mode = qBound(0, mode, 2);
	if (mode == this->mWarpOnChange) return;
	this->mWarpOnChange = mode;
	if (mode == 0) this->cancelWarp();
	emit this->warpOnChangeChanged();
}

void WorkspaceBehavior::setFollowMouse(qint32 mode) {
	mode = qBound(0, mode, 3);
	if (mode == this->mFollowMouse) return;
	this->mFollowMouse = mode;
	this->followOver = nullptr;
	GetCursorPos(&this->followLast);

	if (mode == 1) {
		if (this->followTimer.interval() != 40) {
			this->followTimer.setInterval(40);
			QObject::connect(&this->followTimer, &QTimer::timeout, this, &WorkspaceBehavior::followTick);
		}
		this->followTimer.start();
	} else {
		this->followTimer.stop();
	}
	emit this->followMouseChanged();
}

// follow_mouse = 1: the window the mouse moves onto gets focus (like Hyprland). Only when the mouse itself moved,
// never while a button is held (drags, selections), and never while a menu is open.
void WorkspaceBehavior::followTick() {
	POINT cursor {};
	if (!GetCursorPos(&cursor)) return;
	if (cursor.x == this->followLast.x && cursor.y == this->followLast.y) return;
	this->followLast = cursor;

	if ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000) return;

	// the shell's own see-through layers (bar, panels, borders) cover the screen, so WindowFromPoint mostly finds
	// those: walk the windows top to bottom instead, skipping ours, and take the first one under the cursor
	struct Search {
		POINT cursor;
		HWND found;
	} search {cursor, nullptr};
	EnumWindows(
	    [](HWND hwnd, LPARAM param) -> BOOL {
		    auto* s = reinterpret_cast<Search*>(param); // NOLINT(performance-no-int-to-ptr)
		    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || isOwnProcessWindow(hwnd)) return TRUE;
		    DWORD cloaked = 0;
		    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
		    if (cloaked != 0) return TRUE;
		    RECT r {};
		    if (!GetWindowRect(hwnd, &r) || !PtInRect(&r, s->cursor)) return TRUE;
		    auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		    if ((ex & WS_EX_TRANSPARENT) != 0 && (ex & WS_EX_LAYERED) != 0) return TRUE;   // click-through
		    s->found = hwnd;
		    return FALSE;
	    },
	    reinterpret_cast<LPARAM>(&search)
	);
	auto* root = search.found;
	if (root == nullptr || root == this->followOver) return;
	this->followOver = root;

	auto* foreground = GetForegroundWindow();
	if (root == foreground) return;
	this->watchTracker();
	// only real app windows; another app's popup or menu on top keeps focus where it is
	if (this->tracker->windowFor(root) == nullptr) return;

	// an open menu, a window being moved or a popup of the focused app keeps focus where it is
	GUITHREADINFO info {};
	info.cbSize = sizeof(info);
	if (foreground != nullptr && GetGUIThreadInfo(GetWindowThreadProcessId(foreground, nullptr), &info)) {
		constexpr DWORD BUSY = GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_INMOVESIZE | GUI_SYSTEMMENUMODE;
		if ((info.flags & BUSY) != 0) return;
	}
	if (foreground != nullptr && GetWindow(foreground, GW_OWNER) != nullptr) return;   // a dialog is up

	auto ok = forceForegroundWindow(root);
	if (!ok) qCInfo(logWorkspace) << "follow_mouse: could not focus" << root;
}

void WorkspaceBehavior::onCurrentChanged() {
	auto index = this->desktops->currentIndex();
	const auto& list = this->desktops->desktops();
	auto id = index >= 0 && index < list.length() ? list[index].id : GUID {};

	auto same = isNullGuid(id) || isNullGuid(this->currentId) ? index == this->currentIndex
	                                                           : IsEqualGUID(id, this->currentId);

	if (!same && this->currentIndex >= 0) {
		this->previousId = this->currentId;
		this->previousIndex = this->currentIndex;
	}

	this->currentId = id;
	this->currentIndex = index;
	this->tryWarp();
}

qsizetype WorkspaceBehavior::previousDesktop() const {
	if (this->previousIndex < 0) return -1;
	if (!isNullGuid(this->previousId)) return this->desktops->indexOf(this->previousId);
	return this->previousIndex < this->desktops->count() ? this->previousIndex : -1;
}

void WorkspaceBehavior::watchTracker() {
	if (this->tracker != nullptr) return;
	this->tracker = WindowTracker::instance();

	auto retry = &WorkspaceBehavior::tryWarp;
	QObject::connect(this->tracker, &WindowTracker::activeWindowChanged, this, retry);
	QObject::connect(this->tracker, &WindowTracker::flushed, this, retry);
}

void WorkspaceBehavior::prepareWarp(qsizetype desktop, bool fromActive) {
	this->cancelWarp();
	if (this->mWarpOnChange == 0 || WorkspaceBehavior::cursorOverShell()) return;

	this->watchTracker();
	this->warpFrom = fromActive ? nullptr : this->tracker->activeWindow();
	this->warpDesktop = desktop;
	this->warpDeadline = QDateTime::currentMSecsSinceEpoch() + WARP_TIMEOUT_MS;
}

void WorkspaceBehavior::cancelWarp() {
	this->warpDesktop = -1;
	this->warpFrom = nullptr;
	this->fallbackTimer.stop();
}

void WorkspaceBehavior::tryWarp() {
	if (this->warpDesktop < 0 || this->tracker == nullptr) return;

	if (QDateTime::currentMSecsSinceEpoch() > this->warpDeadline) {
		this->cancelWarp();
		return;
	}

	if (this->desktops->currentIndex() != this->warpDesktop) return;

	auto* window = this->tracker->activeWindow();
	auto qualifies = window != nullptr && window != this->warpFrom.data() && !window->minimized()
	              && (window->desktop() == this->warpDesktop || window->desktop() == -1);

	if (!qualifies) {
		if (!this->fallbackTimer.isActive()) this->fallbackTimer.start();
		return;
	}

	this->cancelWarp();
	WorkspaceBehavior::warpCursor(window->hwnd());
}

void WorkspaceBehavior::warpToTopmost() {
	if (this->warpDesktop < 0 || this->tracker == nullptr) return;

	auto desktop = this->warpDesktop;
	this->cancelWarp();
	if (this->desktops->currentIndex() != desktop) return;

	struct Search {
		WindowTracker* tracker = nullptr;
		qsizetype desktop = -1;
		HWND found = nullptr;
	} search {.tracker = this->tracker, .desktop = desktop};

	EnumWindows(
	    [](HWND hwnd, LPARAM param) -> BOOL {
		    auto* search = reinterpret_cast<Search*>(param); // NOLINT(performance-no-int-to-ptr)
		    auto* window = search->tracker->windowFor(hwnd);
		    if (window == nullptr || window->minimized() || window->desktop() != search->desktop) {
			    return TRUE;
		    }

		    search->found = hwnd;
		    return FALSE;
	    },
	    reinterpret_cast<LPARAM>(&search)
	);

	if (search.found != nullptr) WorkspaceBehavior::warpCursor(search.found);
}

void WorkspaceBehavior::warpToActive() {
	if (this->mWarpOnChange == 0 || WorkspaceBehavior::cursorOverShell()) return;

	this->watchTracker();
	auto* window = this->tracker->activeWindow();
	if (window == nullptr || window->minimized()) return;

	auto desktop = window->desktop();
	if (desktop != -1 && desktop != this->desktops->currentIndex()) return;

	WorkspaceBehavior::warpCursor(window->hwnd());
}

void WorkspaceBehavior::expectFocus(qsizetype desktop) {
	this->focusDesktop = desktop;
	this->focusTimer.start();
}

void WorkspaceBehavior::repairFocus() {
	auto desktop = this->focusDesktop;
	this->focusDesktop = -1;
	if (desktop < 0 || this->desktops->currentIndex() != desktop) return;

	auto* foreground = GetForegroundWindow();
	if (foreground != nullptr && isOwnProcessWindow(foreground)) return;
	if (foreground != nullptr && !isCloaked(foreground) && !isDesktopWindow(foreground)) return;

	this->watchTracker();

	struct Search {
		WindowTracker* tracker = nullptr;
		HWND found = nullptr;
	} search {.tracker = this->tracker};

	EnumWindows(
	    [](HWND hwnd, LPARAM param) -> BOOL {
		    auto* search = reinterpret_cast<Search*>(param); // NOLINT(performance-no-int-to-ptr)
		    if (search->tracker->windowFor(hwnd) == nullptr || IsIconic(hwnd) || isCloaked(hwnd)) {
			    return TRUE;
		    }

		    search->found = hwnd;
		    return FALSE;
	    },
	    reinterpret_cast<LPARAM>(&search)
	);

	if (search.found == nullptr) {
		this->tracker->refreshForeground();
		qCDebug(logWorkspace) << "Nothing to focus on this desktop after a desktop change";
		return;
	}
	if (search.found == foreground) return;

	auto ok = forceForegroundWindow(search.found);
	this->tracker->refreshForeground();
	qCDebug(logWorkspace) << "Nothing on this desktop had focus after a desktop change, moved it to"
	                      << search.found << (ok ? "" : "(failed)");
	if (!ok) qCWarning(logWorkspace) << "Couldn't move focus off a window on another desktop";
}

bool WorkspaceBehavior::cursorOverShell() {
	POINT cursor {};
	if (!GetCursorPos(&cursor)) return false;

	auto* hwnd = WindowFromPoint(cursor);
	if (hwnd == nullptr) return false;

	auto* root = GetAncestor(hwnd, GA_ROOT);
	return isOwnProcessWindow(root != nullptr ? root : hwnd);
}

void WorkspaceBehavior::warpCursor(HWND hwnd) {
	RECT frame {};
	if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) {
		if (!GetWindowRect(hwnd, &frame)) return;
	}

	POINT cursor {};
	if (GetCursorPos(&cursor) && PtInRect(&frame, cursor)) return;

	SetCursorPos((frame.left + frame.right) / 2, (frame.top + frame.bottom) / 2);
}

namespace {
bool gFullscreenApp = false;           // NOLINT
QList<WindowManager*> gWindowManagers; // NOLINT (the QML singleton; one per engine generation)
} // namespace

bool WindowManager::fullscreenApp() { return gFullscreenApp; }

void WindowManager::setFullscreenApp(bool active) {
	if (active == gFullscreenApp) return;
	gFullscreenApp = active;
	for (auto* manager: gWindowManagers) emit manager->fullscreenAppChanged();
}

WindowManager::WindowManager(QObject* parent): QObject(parent) {
	gWindowManagers.append(this);
	// a reloaded shell: start from the mode the window manager already runs in, so setting the same values again
	// is not a change (it used to restart the window manager on every reload)
	this->mKomorebiHyprland = VirtualDesktops::instance()->hyprlandMode();
	this->mKomorebiHyprlandAlt = VirtualDesktops::instance()->hyprlandAltMode();
	QObject::connect(this, &QObject::destroyed, [this]() { gWindowManagers.removeAll(this); });
	auto* decorations = DecorationManager::instance();
	auto* behavior = WorkspaceBehavior::instance();

	// clang-format off
	QObject::connect(decorations, &DecorationManager::bordersChanged, this, &WindowManager::bordersChanged);
	QObject::connect(decorations, &DecorationManager::activeColorChanged, this, &WindowManager::activeBorderColorChanged);
	QObject::connect(decorations, &DecorationManager::inactiveColorChanged, this, &WindowManager::inactiveBorderColorChanged);
	QObject::connect(decorations, &DecorationManager::cornersChanged, this, &WindowManager::cornersChanged);
	QObject::connect(decorations, &DecorationManager::titleBarsChanged, this, &WindowManager::titleBarsChanged);
	QObject::connect(decorations, &DecorationManager::titleBarKeepChanged, this, &WindowManager::titleBarKeepChanged);
	QObject::connect(decorations, &DecorationManager::captionTargetsChanged, this, &WindowManager::captionTargetsChanged);
	QObject::connect(decorations, &DecorationManager::captionHeightChanged, this, &WindowManager::captionHeightChanged);
	QObject::connect(decorations, &DecorationManager::titleBarColorChanged, this, &WindowManager::titleBarColorChanged);
	QObject::connect(decorations, &DecorationManager::titleBarTextColorChanged, this, &WindowManager::titleBarTextColorChanged);
	QObject::connect(behavior, &WorkspaceBehavior::backAndForthChanged, this, &WindowManager::workspaceBackAndForthChanged);
	QObject::connect(behavior, &WorkspaceBehavior::warpOnChangeChanged, this, &WindowManager::warpOnChangeWorkspaceChanged);
	QObject::connect(behavior, &WorkspaceBehavior::followMouseChanged, this, &WindowManager::followMouseChanged);
	// clang-format on
}

bool WindowManager::decorationsSupported() { return DecorationManager::supported(); }

QString WindowManager::borders() const { return DecorationManager::instance()->borders(); }
void WindowManager::setBorders(const QString& borders) {
	DecorationManager::instance()->setBorders(borders);
}

QColor WindowManager::activeBorderColor() const { return DecorationManager::instance()->activeColor(); }
void WindowManager::setActiveBorderColor(const QColor& color) {
	DecorationManager::instance()->setActiveColor(color);
}

QColor WindowManager::inactiveBorderColor() const {
	return DecorationManager::instance()->inactiveColor();
}
void WindowManager::setInactiveBorderColor(const QColor& color) {
	DecorationManager::instance()->setInactiveColor(color);
}

QString WindowManager::corners() const { return DecorationManager::instance()->corners(); }
void WindowManager::setCorners(const QString& corners) {
	DecorationManager::instance()->setCorners(corners);
}

QString WindowManager::titleBars() const { return DecorationManager::instance()->titleBars(); }
void WindowManager::setTitleBars(const QString& titleBars) {
	DecorationManager::instance()->setTitleBars(titleBars);
}

QStringList WindowManager::titleBarKeep() const { return DecorationManager::instance()->titleBarKeep(); }
void WindowManager::setTitleBarKeep(const QStringList& keep) {
	DecorationManager::instance()->setTitleBarKeep(keep);
}

QStringList WindowManager::captionTargets() const { return DecorationManager::instance()->captionTargets(); }
qint32 WindowManager::captionHeight() const { return DecorationManager::instance()->captionHeight(); }
void WindowManager::setCaptionHeight(qint32 height) { DecorationManager::instance()->setCaptionHeight(height); }

QColor WindowManager::titleBarColor() const { return DecorationManager::instance()->titleBarColor(); }
void WindowManager::setTitleBarColor(const QColor& color) {
	DecorationManager::instance()->setTitleBarColor(color);
}

QColor WindowManager::titleBarTextColor() const { return DecorationManager::instance()->titleBarTextColor(); }
void WindowManager::setTitleBarTextColor(const QColor& color) {
	DecorationManager::instance()->setTitleBarTextColor(color);
}

bool WindowManager::workspaceBackAndForth() const {
	return WorkspaceBehavior::instance()->backAndForth();
}
void WindowManager::setWorkspaceBackAndForth(bool enabled) {
	WorkspaceBehavior::instance()->setBackAndForth(enabled);
}

qint32 WindowManager::warpOnChangeWorkspace() const {
	return WorkspaceBehavior::instance()->warpOnChange();
}
void WindowManager::setWarpOnChangeWorkspace(qint32 mode) {
	WorkspaceBehavior::instance()->setWarpOnChange(mode);
}

qint32 WindowManager::followMouse() const { return WorkspaceBehavior::instance()->followMouse(); }
void WindowManager::setFollowMouse(qint32 mode) { WorkspaceBehavior::instance()->setFollowMouse(mode); }

void WindowManager::setKomorebi(const QString& dir) {
	if (dir == this->mKomorebi) return;
	this->mKomorebi = dir;
	VirtualDesktops::instance()->setKomorebi(dir, this->mWhkd, this->mKomorebiHyprland, this->mKomorebiHyprlandAlt);
	emit this->komorebiChanged();
}

void WindowManager::setKomorebiHyprland(bool hyprland) {
	if (hyprland == this->mKomorebiHyprland) return;
	this->mKomorebiHyprland = hyprland;
	VirtualDesktops::instance()->setKomorebiHyprland(hyprland, this->mKomorebiHyprlandAlt, this->mWhkd);
	emit this->komorebiChanged();
}

void WindowManager::setKomorebiHyprlandAlt(bool alt) {
	if (alt == this->mKomorebiHyprlandAlt) return;
	this->mKomorebiHyprlandAlt = alt;
	VirtualDesktops::instance()->setKomorebiHyprland(this->mKomorebiHyprland, alt, this->mWhkd);
	emit this->komorebiChanged();
}

void WindowManager::setWhkd(const QString& path) {
	if (path == this->mWhkd) return;
	this->mWhkd = path;
	emit this->komorebiChanged();
}

} // namespace qs::windows
