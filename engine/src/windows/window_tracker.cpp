#include "window_tracker.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <qfileinfo.h>
#include <qguiapplication.h>
#include <qhash.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpoint.h>
#include <qproperty.h>
#include <qrect.h>
#include <qscreen.h>
#include <qset.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtypes.h>

#include "desktopentry_backend.hpp"
#include "startup.hpp"
#include "util.hpp"
#include "virtual_desktops.hpp"

#include <initguid.h>

#include <appmodel.h>
#include <dwmapi.h>
#include <propkey.h>
#include <propsys.h>
#include <shellapi.h>

#ifndef EVENT_OBJECT_CLOAKED
#define EVENT_OBJECT_CLOAKED 0x8017
#define EVENT_OBJECT_UNCLOAKED 0x8018
#endif

namespace qs::windows {

namespace {
Q_LOGGING_CATEGORY(logTracker, "quickshell.windows.tracker", QtWarningMsg);

constexpr auto EVENT_WINDOW_CLASS = L"QuickshellWindowTrackerEvents";
constexpr UINT WM_QS_WINEVENTS = WM_APP + 1;
constexpr int MOVING_FLUSH_MS = 50;
constexpr int SWEEP_INTERVAL_MS = 10000;

std::mutex gEventMutex;                       // NOLINT
std::vector<WindowEvent> gEvents;             // NOLINT
std::unordered_set<HWND> gTracked;            // NOLINT
std::atomic<bool> gEventWakePending = false;  // NOLINT
std::atomic<HWND> gEventTarget = nullptr;     // NOLINT

bool onlyForTracked(DWORD event) {
	switch (event) {
	case EVENT_OBJECT_LOCATIONCHANGE:
	case EVENT_OBJECT_DESTROY:
	case EVENT_OBJECT_HIDE:
	case EVENT_SYSTEM_MOVESIZESTART:
	case EVENT_SYSTEM_MOVESIZEEND: return true;
	default: return false;
	}
}

bool mayBecomeTracked(HWND hwnd) {
	if (!IsWindowVisible(hwnd)) return false;
	auto exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	return (exStyle & WS_EX_TOOLWINDOW) == 0 || (exStyle & WS_EX_APPWINDOW) != 0;
}

void CALLBACK queueEvent(
    HWINEVENTHOOK /*hook*/,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD /*eventThread*/,
    DWORD eventTime
) {
	if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || hwnd == nullptr) return;

	auto trackedOnly = onlyForTracked(event);
	if (!trackedOnly && event != EVENT_SYSTEM_FOREGROUND && GetAncestor(hwnd, GA_ROOT) != hwnd) {
		return;
	}

	{
		auto lock = std::lock_guard(gEventMutex);

		if (!gTracked.contains(hwnd)) {
			if (trackedOnly) return;

			auto announces = event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_NAMECHANGE;
			if (announces && !mayBecomeTracked(hwnd)) return;
		}

		if (!gEvents.empty() && gEvents.back().event == event && gEvents.back().hwnd == hwnd) return;
		gEvents.push_back({.event = event, .hwnd = hwnd, .time = eventTime});
	}

	if (!gEventWakePending.exchange(true)) {
		auto* target = gEventTarget.load();
		if (target != nullptr) PostMessageW(target, WM_QS_WINEVENTS, 0, 0);
	}
}

void eventThreadMain() {
	MSG msg {};
	PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

	std::vector<HWINEVENTHOOK> hooks;
	auto hook = [&hooks](DWORD min, DWORD max, bool ownProcess) {
		auto* handle = SetWinEventHook(
		    min,
		    max,
		    nullptr,
		    &queueEvent,
		    0,
		    0,
		    WINEVENT_OUTOFCONTEXT | (ownProcess ? 0 : WINEVENT_SKIPOWNPROCESS)
		);

		if (handle != nullptr) hooks.push_back(handle);
	};

	hook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, true);
	hook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, true);
	hook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, true);
	hook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, true);
	hook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_NAMECHANGE, false);
	hook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, true);

	if (hooks.size() < 6) {
		qCWarning(logTracker) << "Only" << hooks.size() << "of 6 window event hooks installed.";
	}

	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	for (auto* handle: hooks) UnhookWinEvent(handle);
}

LRESULT CALLBACK eventWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_QS_WINEVENTS) {
		WindowTracker::instance()->drainEvents();
		return 0;
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

constexpr const wchar_t* UWP_FRAME_CLASS = L"ApplicationFrameWindow";
constexpr const wchar_t* UWP_CORE_CLASS = L"Windows.UI.Core.CoreWindow";

QString windowClass(HWND hwnd) {
	wchar_t buffer[128] {};
	auto length = GetClassNameW(hwnd, buffer, 128);
	return QString::fromWCharArray(buffer, length);
}

bool isShellClass(const QString& cls) {
	static const auto classes = QSet<QString> {
	    QStringLiteral("Windows.UI.Core.CoreWindow"),
	    QStringLiteral("Progman"),
	    QStringLiteral("WorkerW"),
	    QStringLiteral("Shell_TrayWnd"),
	    QStringLiteral("Shell_SecondaryTrayWnd"),
	    QStringLiteral("Windows.Internal.Shell.TabProxyWindow"),
	    QStringLiteral("XamlExplorerHostIslandWindow"),
	    QStringLiteral("TopLevelWindowForOverflowXamlIsland"),
	};

	return classes.contains(cls);
}

QString appUserModelIdOf(HWND hwnd) {
	IPropertyStore* store = nullptr;
	if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store))) || store == nullptr) {
		return {};
	}

	QString id;
	PROPVARIANT value;
	PropVariantInit(&value);

	if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &value)) && value.vt == VT_LPWSTR) {
		id = QString::fromWCharArray(value.pwszVal);
	}

	PropVariantClear(&value);
	store->Release();
	return id;
}

QString appUserModelIdOf(HANDLE process) {
	UINT32 length = 0;
	if (GetApplicationUserModelId(process, &length, nullptr) != ERROR_INSUFFICIENT_BUFFER) return {};
	if (length == 0) return {};

	auto buffer = std::wstring(length, L'\0');
	if (GetApplicationUserModelId(process, &length, buffer.data()) != ERROR_SUCCESS) return {};
	return QString::fromWCharArray(buffer.data());
}

QString processImagePath(HANDLE process) {
	wchar_t buffer[1024] {};
	DWORD length = 1024;
	if (!QueryFullProcessImageNameW(process, 0, buffer, &length)) return {};
	return QString::fromWCharArray(buffer, length);
}

bool covers(const RECT& rect, const QRect& area) {
	return rect.left <= area.left() && rect.top <= area.top()
	    && rect.right >= area.left() + area.width() && rect.bottom >= area.top() + area.height();
}

bool isShown(HWND hwnd) {
	DWORD cloaked = 0;
	return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
	    && cloaked == 0;
}

} // namespace

TrackedWindow::TrackedWindow(WindowTracker* tracker, HWND hwnd)
    : QObject(tracker)
    , tracker(tracker)
    , mHwnd(hwnd) {}

quint64 TrackedWindow::address() const {
	return static_cast<quint64>(reinterpret_cast<quintptr>(this->mHwnd));
}

QString TrackedWindow::addressHex() const { return QString::number(this->address(), 16); }

TrackedWindow* TrackedWindow::owner() const {
	auto* owner = GetWindow(this->mHwnd, GW_OWNER);
	return owner == nullptr ? nullptr : this->tracker->windowFor(owner);
}

void TrackedWindow::refreshIdentity() {
	DWORD pid = 0;
	GetWindowThreadProcessId(this->mHwnd, &pid);

	this->uwpFrame = windowClass(this->mHwnd) == QString::fromWCharArray(UWP_FRAME_CLASS);

	if (this->uwpFrame) {
		auto* core = FindWindowExW(this->mHwnd, nullptr, UWP_CORE_CLASS, nullptr);
		if (core != nullptr) GetWindowThreadProcessId(core, &pid);
	}

	auto* process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

	QString exe;
	auto appId = appUserModelIdOf(this->mHwnd);

	if (process != nullptr) {
		exe = processImagePath(process);
		if (appId.isEmpty()) appId = appUserModelIdOf(process);
		CloseHandle(process);
	}

	if (appId.isEmpty() && !exe.isEmpty()) {
		appId = QFileInfo(exe).fileName().toLower();
		if (appId.endsWith(".exe")) appId.chop(4);
	}

	WindowsDesktopEntryBackend::noteWindowExe(appId, exe);

	Qt::beginPropertyUpdateGroup();
	this->bPid = pid;
	this->bExePath = exe;
	this->bAppId = appId;
	Qt::endPropertyUpdateGroup();
}

void TrackedWindow::refreshTitle() {
	auto length = GetWindowTextLengthW(this->mHwnd);
	QString title;

	if (length > 0) {
		auto buffer = std::wstring(static_cast<size_t>(length) + 1, L'\0');
		auto copied = GetWindowTextW(this->mHwnd, buffer.data(), length + 1);
		title = QString::fromWCharArray(buffer.data(), copied);
	}

	this->bTitle = title;
}

void TrackedWindow::refreshState() {
	auto minimized = IsIconic(this->mHwnd) != FALSE;
	auto maximized = IsZoomed(this->mHwnd) != FALSE;

	auto* monitor = MonitorFromWindow(this->mHwnd, MONITOR_DEFAULTTONEAREST);
	auto* screen = this->tracker->screenFor(monitor);
	auto rects = monitorRects(monitor);

	RECT raw {};
	GetWindowRect(this->mHwnd, &raw);

	RECT frame {};
	if (FAILED(DwmGetWindowAttribute(this->mHwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) {
		frame = raw;
	}

	auto fullscreen = !minimized && !maximized && rects.valid && covers(raw, rects.monitor);

	Qt::beginPropertyUpdateGroup();
	this->bMinimized = minimized;
	this->bMaximized = maximized;
	this->bFullscreen = fullscreen;
	this->bScreen = screen;

	if (screen != nullptr && rects.valid) {
		auto mapper = ScreenMapper(screen->geometry(), rects.monitor, screen->devicePixelRatio());

		if (!minimized) {
			this->bRect = mapper.toLogical(toQRect(frame));
		} else if (this->bRect.value().isNull()) {
			WINDOWPLACEMENT placement {};
			placement.length = sizeof(placement);
			if (GetWindowPlacement(this->mHwnd, &placement)) {
				this->bRect = mapper.toLogical(toQRect(placement.rcNormalPosition));
			}
		}
	}

	Qt::endPropertyUpdateGroup();
}

void TrackedWindow::refreshDesktop(bool askShell) {
	auto* desktops = this->tracker->desktops();
	if (desktops->ownWorkspaces() || desktops->komorebiMode()) {   // they know every window's workspace
		this->bDesktop = static_cast<qint32>(desktops->windowDesktopIndex(this->mHwnd));
		return;
	}

	auto shown = isShown(this->mHwnd);

	if (shown && !askShell && desktops->currentIndex() >= 0) {
		this->bDesktop = static_cast<qint32>(desktops->currentIndex());
		return;
	}

	this->bDesktop = static_cast<qint32>(desktops->windowDesktopIndex(this->mHwnd));
}

void TrackedWindow::activate() {
	auto* desktops = this->tracker->desktops();
	auto desktop = desktops->windowDesktopIndex(this->mHwnd);
	auto switched = false;

	if (desktop != -1 && desktop != desktops->currentIndex()) {
		switched = desktops->switchTo(desktop);
	}

	if (IsIconic(this->mHwnd)) ShowWindowAsync(this->mHwnd, SW_RESTORE);

	auto* target = GetLastActivePopup(this->mHwnd);
	if (target == nullptr || !IsWindowVisible(target)) target = this->mHwnd;

	auto raise = [target]() { forceForegroundWindow(target); };

	if (switched && !desktops->accessorLoaded()) {
		QTimer::singleShot(400, this, raise);
	} else {
		raise();
	}
}

void TrackedWindow::close() {
	if (PostMessageW(this->mHwnd, WM_CLOSE, 0, 0)) return;
	if (GetLastError() != ERROR_ACCESS_DENIED) return;
	PostMessageW(this->mHwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
}

void TrackedWindow::setMinimized(bool minimized) {
	PostMessageW(this->mHwnd, WM_SYSCOMMAND, minimized ? SC_MINIMIZE : SC_RESTORE, 0);
}

void TrackedWindow::setMaximized(bool maximized) {
	PostMessageW(this->mHwnd, WM_SYSCOMMAND, maximized ? SC_MAXIMIZE : SC_RESTORE, 0);
}

void TrackedWindow::setFullscreen(bool fullscreen) {
	this->fullscreenOn(fullscreen ? this->bScreen.value() : nullptr);
}

void TrackedWindow::fullscreenOn(QScreen* screen) {
	auto& backup = this->fullscreenBackup;
	if (IsHungAppWindow(this->mHwnd)) return;

	if (screen == nullptr) {
		if (!backup.active) {
			if (this->bFullscreen.value()) {
				qCInfo(logTracker) << "Window" << this->addressHex()
				                   << "made itself fullscreen; cannot undo that for it.";
			}
			return;
		}

		SetWindowLongPtrW(this->mHwnd, GWL_STYLE, backup.style);
		SetWindowPos(
		    this->mHwnd,
		    nullptr,
		    backup.rect.left,
		    backup.rect.top,
		    backup.rect.right - backup.rect.left,
		    backup.rect.bottom - backup.rect.top,
		    SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS
		);

		backup.active = false;
		return;
	}

	auto rects = monitorRects(monitorForScreen(screen));
	if (!rects.valid) return;

	if (!backup.active) {
		backup.style = GetWindowLongPtrW(this->mHwnd, GWL_STYLE);
		GetWindowRect(this->mHwnd, &backup.rect);
		backup.active = true;
	}

	if (IsZoomed(this->mHwnd)) ShowWindowAsync(this->mHwnd, SW_RESTORE);

	SetWindowLongPtrW(this->mHwnd, GWL_STYLE, backup.style & ~(WS_CAPTION | WS_THICKFRAME));
	SetWindowPos(
	    this->mHwnd,
	    HWND_TOP,
	    rects.monitor.left(),
	    rects.monitor.top(),
	    rects.monitor.width(),
	    rects.monitor.height(),
	    SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS
	);
}

bool TrackedWindow::moveToDesktop(qsizetype index) {
	if (!this->tracker->desktops()->moveWindow(this->mHwnd, index)) return false;
	this->refreshDesktop(true);
	return true;
}

void TrackedWindow::moveTo(const QPoint& logical) {
	auto* screen = QGuiApplication::screenAt(logical);
	if (screen == nullptr) screen = this->bScreen.value();
	if (screen == nullptr) return;

	auto rects = monitorRects(monitorForScreen(screen));
	if (!rects.valid) return;

	auto mapper = ScreenMapper(screen->geometry(), rects.monitor, screen->devicePixelRatio());
	auto physical = mapper.toPhysical(logical);

	RECT raw {};
	RECT frame {};
	GetWindowRect(this->mHwnd, &raw);
	if (FAILED(DwmGetWindowAttribute(this->mHwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) {
		frame = raw;
	}

	SetWindowPos(
	    this->mHwnd,
	    nullptr,
	    physical.x() - (frame.left - raw.left),
	    physical.y() - (frame.top - raw.top),
	    0,
	    0,
	    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS
	);
}

WindowTracker* WindowTracker::instance() {
	static auto* instance = new WindowTracker(); // NOLINT
	return instance;
}

WindowTracker::WindowTracker() {
	this->mDesktops = VirtualDesktops::instance();

	this->flushTimer.setSingleShot(true);
	QObject::connect(&this->flushTimer, &QTimer::timeout, this, &WindowTracker::flush);

	this->sweepTimer.setInterval(SWEEP_INTERVAL_MS);
	QObject::connect(&this->sweepTimer, &QTimer::timeout, this, [this]() {
		if (this->sweepDestroyed()) {
			this->updateActive();
			emit this->flushed();
		}
	});
	this->sweepTimer.start();

	this->updateScreens();

	if (auto* app = qobject_cast<QGuiApplication*>(QGuiApplication::instance())) {
		QObject::connect(app, &QGuiApplication::screenAdded, this, &WindowTracker::onScreensChanged);
		QObject::connect(app, &QGuiApplication::screenRemoved, this, [this](QScreen* screen) {
			for (auto* window: this->mWindows) window->forgetScreen(screen);
			this->onScreensChanged();
		});
	}

	QObject::connect(
	    this->mDesktops,
	    &VirtualDesktops::desktopsChanged,
	    this,
	    &WindowTracker::onDesktopsChanged
	);

	QObject::connect(this->mDesktops, &VirtualDesktops::currentChanged, this, [this]() {
		for (auto* window: this->mWindows) {
			if (isShown(window->hwnd())) window->refreshDesktop(false);
		}

		this->foregroundDirty = true;
		this->schedule();
	});

	this->startEventThread();

	startup::afterFirstFrame(this, [this]() { this->rescan(); });
}

WindowTracker::~WindowTracker() = default;

void WindowTracker::startEventThread() {
	WNDCLASSW wndClass {};
	wndClass.lpfnWndProc = &eventWindowProc;
	wndClass.hInstance = GetModuleHandleW(nullptr);
	wndClass.lpszClassName = EVENT_WINDOW_CLASS;
	RegisterClassW(&wndClass);

	auto* window = CreateWindowExW(
	    0,
	    EVENT_WINDOW_CLASS,
	    L"",
	    0,
	    0,
	    0,
	    0,
	    0,
	    HWND_MESSAGE,
	    nullptr,
	    wndClass.hInstance,
	    nullptr
	);

	if (window == nullptr) {
		qCWarning(logTracker) << "Failed to create the event window, windows won't be tracked live.";
		return;
	}

	gEventTarget.store(window);
	std::thread(&eventThreadMain).detach();
}

void WindowTracker::drainEvents() {
	gEventWakePending.store(false);

	auto events = std::move(this->spareEvents);
	events.clear();

	{
		auto lock = std::lock_guard(gEventMutex);
		events.swap(gEvents);
	}

	for (const auto& e: events) this->onEvent(e.event, e.hwnd, e.time);

	events.clear();
	this->spareEvents = std::move(events);
}

void WindowTracker::onEvent(DWORD event, HWND hwnd, DWORD time) {
	auto tracked = this->byHwnd.contains(hwnd);

	switch (event) {
	case EVENT_SYSTEM_FOREGROUND:
		this->foregroundDirty = true;
		if (!tracked) this->candidates.insert(hwnd);
		emit this->foregroundChanged(hwnd, time);
		break;
	case EVENT_OBJECT_LOCATIONCHANGE:
		if (!tracked) return;
		this->dirty[hwnd].state = true;

		if (this->moving.contains(hwnd)) {
			if (!this->flushTimer.isActive()) this->flushTimer.start(MOVING_FLUSH_MS);
			return;
		}
		break;
	case EVENT_OBJECT_DESTROY:
	case EVENT_OBJECT_HIDE:
		if (!tracked) return;
		this->dirty[hwnd].eligibility = true;
		break;
	case EVENT_OBJECT_SHOW:
		if (tracked) this->dirty[hwnd].eligibility = true;
		else this->candidates.insert(hwnd);
		break;
	case EVENT_OBJECT_NAMECHANGE:
		if (tracked) this->dirty[hwnd].title = true;
		else this->candidates.insert(hwnd);
		break;
	case EVENT_OBJECT_CLOAKED:
	case EVENT_OBJECT_UNCLOAKED:
		this->mDesktops->noteCloakChange();
		this->foregroundDirty = true;
		if (tracked) {
			auto& d = this->dirty[hwnd];
			d.eligibility = true;
			d.desktop = true;
			if (event == EVENT_OBJECT_CLOAKED) d.askShell = true;
			d.state = true;
		} else {
			this->candidates.insert(hwnd);
		}
		break;
	case EVENT_SYSTEM_MINIMIZESTART:
	case EVENT_SYSTEM_MINIMIZEEND:
		if (tracked) this->dirty[hwnd].state = true;
		else this->candidates.insert(hwnd);
		break;
	case EVENT_SYSTEM_MOVESIZESTART:
	case EVENT_SYSTEM_MOVESIZEEND:
		if (!tracked) return;
		this->dirty[hwnd].state = true;

		if (event == EVENT_SYSTEM_MOVESIZESTART) {
			this->moving.insert(hwnd);
			emit this->moveSizeStarted(this->byHwnd.value(hwnd));
		} else {
			this->moving.remove(hwnd);
			emit this->moveSizeEnded(this->byHwnd.value(hwnd));
		}
		break;
	default: return;
	}

	this->schedule();
}

void WindowTracker::noteMoveSize(HWND hwnd, bool started) {
	auto event = started ? EVENT_SYSTEM_MOVESIZESTART : EVENT_SYSTEM_MOVESIZEEND;
	this->onEvent(event, hwnd, GetTickCount());
}

void WindowTracker::schedule() {
	if (!this->flushTimer.isActive() || this->flushTimer.interval() != 0) this->flushTimer.start(0);
}

void WindowTracker::flush() {
	auto candidates = std::move(this->candidates);
	this->candidates.clear();

	for (auto* hwnd: candidates) {
		if (!this->byHwnd.contains(hwnd) && this->isEligible(hwnd)) {
			this->addWindow(hwnd);
			if (hwnd == GetForegroundWindow()) this->foregroundDirty = true;
		}
	}

	auto dirty = std::move(this->dirty);
	this->dirty.clear();

	for (auto iter = dirty.constBegin(); iter != dirty.constEnd(); ++iter) {
		auto* window = this->byHwnd.value(iter.key());
		if (window == nullptr) continue;

		const auto& d = iter.value();

		if (d.eligibility && !this->isEligible(iter.key())) {
			this->removeWindow(window);
			continue;
		}

		Qt::beginPropertyUpdateGroup();
		if (d.title) window->refreshTitle();
		if (d.state) window->refreshState();
		if (d.desktop) window->refreshDesktop(d.askShell);
		if (d.desktop && window->uwpFrame) window->refreshIdentity();
		Qt::endPropertyUpdateGroup();
	}

	if (this->foregroundDirty) {
		this->foregroundDirty = false;
		this->sweepDestroyed();
		this->updateActive();
	}

	emit this->flushed();
}

bool WindowTracker::sweepDestroyed() {
	auto removed = false;

	for (auto* window: QList(this->mWindows)) {
		if (!IsWindow(window->hwnd())) {
			this->removeWindow(window);
			removed = true;
		}
	}

	return removed;
}

bool WindowTracker::isEligible(HWND hwnd) const {
	if (hwnd == nullptr || !IsWindow(hwnd)) return false;
	// a window on another of Caelestia's own workspaces is hidden, but still one of yours
	if (!IsWindowVisible(hwnd) && !this->mDesktops->hidesWindow(hwnd)) return false;
	if (GetAncestor(hwnd, GA_ROOT) != hwnd) return false;

	auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
	if ((style & WS_CHILD) != 0) return false;

	auto exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	auto appWindow = (exStyle & WS_EX_APPWINDOW) != 0;
	if ((exStyle & WS_EX_TOOLWINDOW) != 0 && !appWindow) return false;

	if (!appWindow) {
		HWND walk = nullptr;
		auto* next = GetAncestor(hwnd, GA_ROOTOWNER);

		while (next != walk) {
			walk = next;
			next = GetLastActivePopup(walk);
			if (IsWindowVisible(next)) break;
		}

		if (walk != hwnd) return false;
	}

	if (isShellClass(windowClass(hwnd))) return false;
	if (GetWindowTextLengthW(hwnd) == 0) return false;

	DWORD cloaked = 0;
	DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));

	if (cloaked != 0 && this->mDesktops->isWindowOnCurrent(hwnd)) return false;

	return true;
}

void WindowTracker::addWindow(HWND hwnd) {
	auto* window = new TrackedWindow(this, hwnd);

	Qt::beginPropertyUpdateGroup();
	window->refreshIdentity();
	window->refreshTitle();
	window->refreshState();
	window->refreshDesktop(false);
	Qt::endPropertyUpdateGroup();

	this->mWindows.append(window);
	this->byHwnd.insert(hwnd, window);

	{
		auto lock = std::lock_guard(gEventMutex);
		gTracked.insert(hwnd);
	}

	qCDebug(logTracker) << "Tracking" << window->addressHex() << window->appId() << window->title();
	emit this->windowAdded(window);
}

void WindowTracker::removeWindow(TrackedWindow* window) {
	qCDebug(logTracker) << "Dropping" << window->addressHex() << window->title();

	this->mWindows.removeOne(window);
	this->byHwnd.remove(window->hwnd());
	this->moving.remove(window->hwnd());

	{
		auto lock = std::lock_guard(gEventMutex);
		gTracked.erase(window->hwnd());
	}

	if (this->mActive == window) this->setActive(nullptr);

	emit this->windowRemoved(window);
	emit window->closed();
	window->deleteLater();
}

void WindowTracker::updateActive() {
	auto* foreground = GetForegroundWindow();

	if (foreground == nullptr) {
		this->setActive(nullptr);
		return;
	}

	auto* window = this->byHwnd.value(foreground);
	if (window == nullptr && isOwnProcessWindow(foreground) && !this->isEligible(foreground)) {
		if (this->mActive != nullptr && !isShown(this->mActive->hwnd())) this->setActive(nullptr);
		return;
	}

	if (window == nullptr && this->isEligible(foreground)) {
		this->addWindow(foreground);
		window = this->byHwnd.value(foreground);
	}

	if (window != nullptr && !isShown(foreground)) {
		qCDebug(logTracker) << "Foreground" << window->addressHex() << "is on another desktop";
		window = nullptr;
	}

	this->setActive(window);
}

void WindowTracker::refreshForeground() {
	this->foregroundDirty = true;
	this->schedule();
}

void WindowTracker::setActive(TrackedWindow* window) {
	if (window == this->mActive) return;

	Qt::beginPropertyUpdateGroup();
	if (this->mActive != nullptr) this->mActive->bActivated = false;
	this->mActive = window;
	if (window != nullptr) window->bActivated = true;
	Qt::endPropertyUpdateGroup();

	emit this->activeWindowChanged();
}

void WindowTracker::rescan() {
	QSet<HWND> seen;

	EnumWindows(
	    [](HWND hwnd, LPARAM param) -> BOOL {
		    auto* seen = reinterpret_cast<QSet<HWND>*>(param); // NOLINT(performance-no-int-to-ptr)
		    seen->insert(hwnd);
		    return TRUE;
	    },
	    reinterpret_cast<LPARAM>(&seen)
	);

	for (auto* window: QList(this->mWindows)) {
		if (!seen.contains(window->hwnd()) || !this->isEligible(window->hwnd())) {
			this->removeWindow(window);
		}
	}

	for (auto* hwnd: seen) {
		if (!this->byHwnd.contains(hwnd) && this->isEligible(hwnd)) this->addWindow(hwnd);
	}

	this->updateActive();
	emit this->flushed();
}

QScreen* WindowTracker::screenFor(HMONITOR monitor) const {
	return this->screensByMonitor.value(monitor);
}

qsizetype WindowTracker::screenIndex(QScreen* screen) {
	return QGuiApplication::screens().indexOf(screen);
}

void WindowTracker::updateScreens() {
	this->screensByMonitor.clear();

	for (auto* screen: QGuiApplication::screens()) {
		auto* monitor = monitorForScreen(screen);
		if (monitor != nullptr) this->screensByMonitor.insert(monitor, screen);
	}
}

void TrackedWindow::forgetScreen(QScreen* screen) {
	if (this->bScreen.value() == screen) this->bScreen = nullptr;
}

void WindowTracker::onScreensChanged() {
	this->updateScreens();
	for (auto* window: this->mWindows) this->dirty[window->hwnd()].state = true;
	this->schedule();
}

void WindowTracker::onDesktopsChanged() {
	for (auto* window: this->mWindows) this->dirty[window->hwnd()].desktop = true;
	this->schedule();
}

} // namespace qs::windows
