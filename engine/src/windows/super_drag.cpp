#include "super_drag.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <thread>

#include <qcoreapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpoint.h>
#include <qpointer.h>

#include "input_mask.hpp"
#include "keyboard_hook.hpp"
#include "tiling.hpp"
#include "util.hpp"
#include "window_tracker.hpp"

#include <dwmapi.h>

namespace qs::windows {

namespace {

Q_LOGGING_CATEGORY(logSuperDrag, "quickshell.windows.superdrag", QtWarningMsg);

constexpr auto MESSAGE_WINDOW_CLASS = L"QuickshellSuperDrag";

constexpr UINT WM_SD_BEGIN_MOVE = WM_APP + 1;
constexpr UINT WM_SD_BEGIN_RESIZE = WM_APP + 2;
constexpr UINT WM_SD_MOVE = WM_APP + 3;
constexpr UINT WM_SD_END = WM_APP + 4;

constexpr UINT WM_SD_STARTED = WM_APP + 1;
constexpr UINT WM_SD_TILED_RESIZE = WM_APP + 2;
constexpr UINT WM_SD_ENDED = WM_APP + 3;
constexpr UINT WM_SD_WHEEL = WM_APP + 4;

constexpr LPARAM FLAG_RESIZE = 1;
constexpr LPARAM FLAG_LEFT = 2;
constexpr LPARAM FLAG_TOP = 4;

constexpr LRESULT STARTED_TILED_RESIZE = 1;
constexpr LRESULT STARTED_WATCHED = 2;

constexpr UINT GUI_TIMEOUT_MS = 250;
constexpr DWORD IN_FLIGHT_TIMEOUT_MS = 50;
constexpr DWORD RETRY_MS = 4;
constexpr DWORD SETTLE_TIMEOUT_MS = 150;
constexpr DWORD SETTLE_STEP_MS = 5;
constexpr LONG CLAMP_SLACK = 32;
constexpr DWORD ELEVATION_CACHE_MS = 2000;
constexpr DWORD WHEEL_RESET_MS = 400;

constexpr UINT POS_FLAGS = SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS;

static_assert(sizeof(LPARAM) >= sizeof(uint64_t));

std::atomic<bool> gEnabled = false;      // NOLINT
std::atomic<DWORD> gWorkerId = 0;        // NOLINT
std::atomic<bool> gWorkerStop = false;   // NOLINT
std::atomic<HWND> gGuiWindow = nullptr;  // NOLINT
std::atomic<uint64_t> gCursor = 0;       // NOLINT
std::atomic<bool> gMovePending = false;  // NOLINT
std::atomic<uint64_t> gTiledDelta = 0;   // NOLINT
std::atomic<bool> gTiledPending = false; // NOLINT
std::atomic<bool> gWheelColumns = false; // NOLINT
std::atomic<bool> gDragAlt = false;      // NOLINT - the drag modifier is Alt (Hyprland bindm = ALT ...) instead of Super

struct HookState {
	bool active = false;
	bool resize = false;
	DWORD elevationPid = 0;
	DWORD elevationTime = 0;
	bool elevated = false;
	qint32 wheel = 0;
	DWORD wheelTime = 0;
};

HookState gHook; // NOLINT

uint64_t pack(LONG x, LONG y) {
	return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y);
}

uint64_t pack(POINT point) { return pack(point.x, point.y); }

POINT unpack(uint64_t value) {
	return POINT {
	    .x = static_cast<LONG>(static_cast<int32_t>(static_cast<uint32_t>(value >> 32))),
	    .y = static_cast<LONG>(static_cast<int32_t>(static_cast<uint32_t>(value))),
	};
}

LONG widthOf(const RECT& rect) { return rect.right - rect.left; }
LONG heightOf(const RECT& rect) { return rect.bottom - rect.top; }

bool isDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool superAlone() {
	if (gDragAlt.load(std::memory_order_relaxed)) {
		if (!isDown(VK_MENU)) return false;
		return !isDown(VK_CONTROL) && !isDown(VK_LWIN) && !isDown(VK_RWIN) && !isDown(VK_SHIFT);
	}
	if (!isDown(VK_LWIN) && !isDown(VK_RWIN)) return false;
	return !isDown(VK_CONTROL) && !isDown(VK_MENU) && !isDown(VK_SHIFT);
}

bool isShellWindow(HWND hwnd) {
	static constexpr std::array<const wchar_t*, 13> CLASSES {
	    L"Progman",
	    L"WorkerW",
	    L"Shell_TrayWnd",
	    L"Shell_SecondaryTrayWnd",
	    L"NotifyIconOverflowWindow",
	    L"TopLevelWindowForOverflowXamlIsland",
	    L"Windows.UI.Core.CoreWindow",
	    L"XamlExplorerHostIslandWindow",
	    L"Windows.Internal.Shell.TabProxyWindow",
	    L"MultitaskingViewFrame",
	    L"TaskListThumbnailWnd",
	    L"Shell_InputSwitchTopLevelWindow",
	    L"#32768",
	};

	std::array<wchar_t, 128> name {};
	if (GetClassNameW(hwnd, name.data(), static_cast<int>(name.size())) == 0) return true;

	return std::ranges::any_of(CLASSES, [&name](const wchar_t* cls) {
		return std::wcscmp(name.data(), cls) == 0;
	});
}

bool coversMonitor(HWND hwnd) {
	RECT rect {};
	if (!GetWindowRect(hwnd, &rect)) return false;

	MONITORINFO info {};
	info.cbSize = sizeof(info);
	if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) return false;

	const auto& monitor = info.rcMonitor;
	return rect.left <= monitor.left && rect.top <= monitor.top && rect.right >= monitor.right
	    && rect.bottom >= monitor.bottom;
}

bool isElevatedCached(HWND hwnd) {
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	auto now = GetTickCount();

	if (pid != 0 && pid == gHook.elevationPid && now - gHook.elevationTime < ELEVATION_CACHE_MS) {
		return gHook.elevated;
	}

	gHook.elevated = isMoreElevated(hwnd);
	gHook.elevationPid = pid;
	gHook.elevationTime = now;
	return gHook.elevated;
}

// the shell's own see-through layers (bar, panels, borders) cover the screen and WindowFromPoint finds them first:
// then walk the windows top to bottom past ours to the app window under the cursor
HWND appWindowAt(POINT point) {
	auto* hit = WindowFromPhysicalPoint(point);
	auto* root = hit != nullptr ? GetAncestor(hit, GA_ROOT) : nullptr;
	if (root != nullptr && !isOwnProcessWindow(root)) return root;

	struct Search {
		POINT point;
		HWND found;
	} search {point, nullptr};
	EnumWindows(
	    [](HWND hwnd, LPARAM param) -> BOOL {
		    auto* s = reinterpret_cast<Search*>(param); // NOLINT(performance-no-int-to-ptr)
		    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || isOwnProcessWindow(hwnd)) return TRUE;
		    auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		    if ((ex & WS_EX_TRANSPARENT) != 0 && (ex & WS_EX_LAYERED) != 0) return TRUE;
		    DWORD cloaked = 0;
		    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
		    if (cloaked != 0) return TRUE;
		    RECT r {};
		    if (!GetWindowRect(hwnd, &r) || !PtInRect(&r, s->point)) return TRUE;
		    s->found = hwnd;
		    return FALSE;
	    },
	    reinterpret_cast<LPARAM>(&search)
	);
	return search.found;
}

HWND dragTarget(POINT point) {
	auto* hwnd = appWindowAt(point);
	if (hwnd == nullptr || isOwnProcessWindow(hwnd)) return nullptr;
	if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return nullptr;

	auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
	auto exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	if ((style & WS_CHILD) != 0) return nullptr;

	auto caption = (style & WS_CAPTION) == WS_CAPTION;
	if (!caption && (style & WS_THICKFRAME) == 0) return nullptr;
	if ((exStyle & WS_EX_TOOLWINDOW) != 0 && (exStyle & WS_EX_APPWINDOW) == 0) return nullptr;

	auto maximized = IsZoomed(hwnd) != FALSE;
	if (isShellWindow(hwnd)) return nullptr;

	DWORD cloaked = 0;
	DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
	if (cloaked != 0) return nullptr;

	if (!maximized && coversMonitor(hwnd)) return nullptr;
	if (isElevatedCached(hwnd)) return nullptr;

	return hwnd;
}

bool postWorker(UINT message, WPARAM wParam, LPARAM lParam) {
	auto id = gWorkerId.load();
	return id != 0 && PostThreadMessageW(id, message, wParam, lParam) != FALSE;
}

struct Drag {
	HWND hwnd = nullptr;
	bool alive = false;
	bool notified = false;
	bool resize = false;
	bool tiled = false;
	bool watched = false;
	bool left = false;
	bool top = false;
	bool inFlight = false;
	bool dirty = false;
	DWORD sentAt = 0;
	POINT start {};
	POINT last {};
	RECT origin {};
	RECT requested {};
	RECT before {};
	LONG minWidth = 1;
	LONG minHeight = 1;
	LONG maxWidth = LONG_MAX;
	LONG maxHeight = LONG_MAX;
};

template <typename Predicate>
bool settle(Predicate done) {
	auto start = GetTickCount();

	while (!done()) {
		if (GetTickCount() - start >= SETTLE_TIMEOUT_MS) return false;
		Sleep(SETTLE_STEP_MS);
	}

	return true;
}

void setRect(HWND hwnd, const RECT& rect) {
	SetWindowPos(hwnd, nullptr, rect.left, rect.top, widthOf(rect), heightOf(rect), POS_FLAGS);
}

void setPosition(HWND hwnd, LONG x, LONG y) {
	SetWindowPos(hwnd, nullptr, x, y, 0, 0, POS_FLAGS | SWP_NOSIZE);
}

bool matchesRequest(const Drag& drag, const RECT& actual) {
	if (drag.resize) return EqualRect(&actual, &drag.requested) != FALSE;
	return actual.left == drag.requested.left && actual.top == drag.requested.top;
}

void send(Drag& drag, const RECT& target) {
	if (!GetWindowRect(drag.hwnd, &drag.before)) drag.before = drag.requested;
	drag.requested = target;
	drag.inFlight = true;
	drag.sentAt = GetTickCount();

	if (drag.resize) setRect(drag.hwnd, target);
	else setPosition(drag.hwnd, target.left, target.top);
}

bool stillInFlight(const Drag& drag) {
	if (!drag.inFlight || GetTickCount() - drag.sentAt >= IN_FLIGHT_TIMEOUT_MS) return false;

	RECT actual {};
	if (!GetWindowRect(drag.hwnd, &actual)) return false;
	return EqualRect(&actual, &drag.before) != FALSE && !matchesRequest(drag, actual);
}

bool restoreUnderCursor(Drag& drag) {
	WINDOWPLACEMENT placement {};
	placement.length = sizeof(placement);
	if (!GetWindowPlacement(drag.hwnd, &placement)) return false;

	auto width = widthOf(placement.rcNormalPosition);
	auto height = heightOf(placement.rcNormalPosition);
	if (width <= 0 || height <= 0) return false;

	auto frame = drag.origin;
	auto hr = DwmGetWindowAttribute(drag.hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame));
	if (FAILED(hr)) frame = drag.origin;

	auto frameWidth = std::max(widthOf(frame), LONG(1));
	auto frameHeight = std::max(heightOf(frame), LONG(1));
	auto ratio = std::clamp(static_cast<double>(drag.start.x - frame.left) / frameWidth, 0.0, 1.0);
	auto offsetY = std::clamp(drag.start.y - frame.top, LONG(0), frameHeight);
	auto scaledY = std::lround(static_cast<double>(offsetY) / frameHeight * height);
	auto grabY = offsetY < height / 2 ? offsetY : static_cast<LONG>(scaledY);

	auto left = drag.start.x - static_cast<LONG>(std::lround(ratio * width));
	auto top = drag.start.y - grabY;

	ShowWindowAsync(drag.hwnd, SW_SHOWNOACTIVATE);
	if (!settle([&drag]() { return IsZoomed(drag.hwnd) == FALSE; })) return false;

	drag.origin = RECT {.left = left, .top = top, .right = left + width, .bottom = top + height};
	send(drag, drag.origin);
	return true;
}

void beginDrag(Drag& drag, HWND hwnd, bool resize, POINT point) {
	hotkeys::KeyboardHook::sendKey(hotkeys::KeyboardHook::MASK_KEY);

	drag = Drag();
	drag.hwnd = hwnd;
	drag.resize = resize;
	drag.start = point;
	drag.last = point;

	if (!GetWindowRect(hwnd, &drag.origin)) return;
	drag.requested = drag.origin;

	auto maximized = IsZoomed(hwnd) != FALSE;
	auto sizable = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0;

	qCDebug(logSuperDrag) << (resize ? "Resize" : "Move") << "drag of" << hwnd << "from" << point.x
	                      << point.y << "maximized" << maximized << "sizable" << sizable;

	if (resize && (maximized || !sizable)) return;

	drag.left = point.x < drag.origin.left + widthOf(drag.origin) / 2;
	drag.top = point.y < drag.origin.top + heightOf(drag.origin) / 2;

	auto dpi = GetDpiForWindow(hwnd);
	if (dpi == 0) dpi = USER_DEFAULT_SCREEN_DPI;

	drag.minWidth = std::clamp(
	    static_cast<LONG>(GetSystemMetricsForDpi(SM_CXMINTRACK, dpi)),
	    LONG(1),
	    std::max(widthOf(drag.origin), LONG(1))
	);
	drag.minHeight = std::clamp(
	    static_cast<LONG>(GetSystemMetricsForDpi(SM_CYMINTRACK, dpi)),
	    LONG(1),
	    std::max(heightOf(drag.origin), LONG(1))
	);
	drag.maxWidth =
	    std::max(static_cast<LONG>(GetSystemMetricsForDpi(SM_CXMAXTRACK, dpi)), drag.minWidth);
	drag.maxHeight =
	    std::max(static_cast<LONG>(GetSystemMetricsForDpi(SM_CYMAXTRACK, dpi)), drag.minHeight);

	if (auto* gui = gGuiWindow.load()) {
		auto flags =
		    (resize ? FLAG_RESIZE : 0) | (drag.left ? FLAG_LEFT : 0) | (drag.top ? FLAG_TOP : 0);
		DWORD_PTR result = 0;

		auto sent = SendMessageTimeoutW(
		    gui,
		    WM_SD_STARTED,
		    reinterpret_cast<WPARAM>(hwnd),
		    flags,
		    SMTO_NORMAL | SMTO_ABORTIFHUNG,
		    GUI_TIMEOUT_MS,
		    &result
		);

		drag.notified = true;
		if (sent != 0) {
			drag.tiled = (result & STARTED_TILED_RESIZE) != 0;
			drag.watched = (result & STARTED_WATCHED) != 0;
		}

		qCDebug(logSuperDrag) << "Shell answered" << (sent != 0) << "tiled" << drag.tiled << "watched"
		                      << drag.watched;
	}

	if (!drag.tiled) {
		constexpr UINT raiseFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS;
		auto topmost = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
		SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, raiseFlags);
		if (!topmost) SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, raiseFlags);
	}

	if (maximized && !restoreUnderCursor(drag)) return;
	drag.alive = true;
}

void learnLimits(Drag& drag) {
	if (!drag.left && !drag.top) return;

	RECT actual {};
	if (!GetWindowRect(drag.hwnd, &actual)) return;

	auto learn = [](LONG wanted, LONG got, LONG& minimum, LONG& maximum) {
		if (got - wanted > CLAMP_SLACK) minimum = std::max(minimum, got);
		else if (wanted - got > CLAMP_SLACK) maximum = std::min(maximum, got);
	};

	if (drag.left && actual.left == drag.requested.left) {
		learn(widthOf(drag.requested), widthOf(actual), drag.minWidth, drag.maxWidth);
	}

	if (drag.top && actual.top == drag.requested.top) {
		learn(heightOf(drag.requested), heightOf(actual), drag.minHeight, drag.maxHeight);
	}
}

RECT resizedRect(const Drag& drag, LONG dx, LONG dy) {
	auto rect = drag.origin;

	auto width = std::clamp(
	    widthOf(rect) + (drag.left ? -dx : dx),
	    drag.minWidth,
	    std::max(drag.minWidth, drag.maxWidth)
	);
	auto height = std::clamp(
	    heightOf(rect) + (drag.top ? -dy : dy),
	    drag.minHeight,
	    std::max(drag.minHeight, drag.maxHeight)
	);

	if (drag.left) rect.left = rect.right - width;
	else rect.right = rect.left + width;

	if (drag.top) rect.top = rect.bottom - height;
	else rect.bottom = rect.top + height;

	return rect;
}

void updateDrag(Drag& drag, POINT point, bool force) {
	if (!drag.alive) return;

	drag.last = point;
	auto dx = point.x - drag.start.x;
	auto dy = point.y - drag.start.y;

	if (drag.tiled) {
		gTiledDelta.store(pack(dx, dy));
		auto* gui = gGuiWindow.load();

		if (gui != nullptr && !gTiledPending.exchange(true)) {
			if (!PostMessageW(gui, WM_SD_TILED_RESIZE, 0, 0)) gTiledPending.store(false);
		}

		return;
	}

	auto target = drag.origin;

	if (drag.resize) {
		learnLimits(drag);
		target = resizedRect(drag, dx, dy);
	} else {
		OffsetRect(&target, dx, dy);

		RECT current {};
		auto originWidth = widthOf(drag.origin);
		auto originHeight = heightOf(drag.origin);
		if (GetWindowRect(drag.hwnd, &current) && originWidth > 0 && originHeight > 0
		    && (widthOf(current) != originWidth || heightOf(current) != originHeight))
		{
			auto fx = static_cast<double>(drag.start.x - drag.origin.left) / originWidth;
			auto fy = static_cast<double>(drag.start.y - drag.origin.top) / originHeight;
			target.left = point.x - std::lround(fx * widthOf(current));
			target.top = point.y - std::lround(fy * heightOf(current));
			target.right = target.left + widthOf(current);
			target.bottom = target.top + heightOf(current);
		}
	}

	if (matchesRequest(drag, target)) {
		drag.dirty = false;
		return;
	}

	if (!force && stillInFlight(drag)) {
		drag.dirty = true;
		return;
	}

	drag.dirty = false;
	send(drag, target);
}

void anchorResized(Drag& drag) {
	if (!drag.left && !drag.top) return;

	RECT actual {};
	if (!GetWindowRect(drag.hwnd, &actual)) return;
	if (drag.left && actual.left != drag.requested.left) return;
	if (drag.top && actual.top != drag.requested.top) return;

	auto x = drag.left ? drag.origin.right - widthOf(actual) : actual.left;
	auto y = drag.top ? drag.origin.bottom - heightOf(actual) : actual.top;
	if (x == actual.left && y == actual.top) return;

	setPosition(drag.hwnd, x, y);
	if (!drag.watched) return;

	auto* hwnd = drag.hwnd;
	settle([hwnd, x, y]() {
		RECT rect {};
		return !GetWindowRect(hwnd, &rect) || (rect.left == x && rect.top == y);
	});
}

void finishDrag(Drag& drag) {
	if (drag.alive && !drag.tiled && (drag.watched || drag.resize)) {
		settle([&drag]() {
			RECT actual {};
			return !GetWindowRect(drag.hwnd, &actual) || matchesRequest(drag, actual);
		});

		if (drag.resize) anchorResized(drag);
	}

	if (drag.notified) {
		if (auto* gui = gGuiWindow.load()) {
			auto delta = pack(drag.last.x - drag.start.x, drag.last.y - drag.start.y);
			auto target = reinterpret_cast<WPARAM>(drag.hwnd);
			PostMessageW(gui, WM_SD_ENDED, target, static_cast<LPARAM>(delta));
		}
	}

	drag = Drag();
}

} // namespace

SuperDragManager* SuperDragManager::instance() {
	static QPointer<SuperDragManager> manager; // NOLINT

	if (manager.isNull()) {
		manager = new SuperDragManager(QCoreApplication::instance());
	}

	return manager.data();
}

SuperDragManager::SuperDragManager(QObject* parent): QObject(parent) {
	WNDCLASSW wndClass {};
	wndClass.lpfnWndProc = &SuperDragManager::messageWindowProc;
	wndClass.hInstance = GetModuleHandleW(nullptr);
	wndClass.lpszClassName = MESSAGE_WINDOW_CLASS;
	RegisterClassW(&wndClass);

	this->messageWindow = CreateWindowExW(
	    0,
	    MESSAGE_WINDOW_CLASS,
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

	if (this->messageWindow == nullptr) {
		qCWarning(logSuperDrag) << "Failed to create the message window,"
		                        << "tiling won't follow Super drags.";
	}

	gGuiWindow.store(this->messageWindow);
}

SuperDragManager::~SuperDragManager() {
	gEnabled.store(false);
	gGuiWindow.store(nullptr);
	this->stopWorker();

	if (this->messageWindow != nullptr) {
		DestroyWindow(this->messageWindow);
		this->messageWindow = nullptr;
	}
}

void SuperDragManager::setEnabled(bool enabled) {
	if (enabled == this->mEnabled) return;

	if (enabled && !this->startWorker()) {
		qCWarning(logSuperDrag) << "Failed to start the drag thread, Super drags are unavailable.";
		return;
	}

	this->mEnabled = enabled;
	gEnabled.store(enabled);

	auto* tracker = InputMaskTracker::instance();

	if (enabled) {
		tracker->acquireHook();
		if (!tracker->hookActive()) {
			qCWarning(logSuperDrag) << "Mouse hook unavailable, Super drags won't work.";
		}
	} else {
		tracker->releaseHook();
	}

	emit this->enabledChanged();
}

void SuperDragManager::setWheelColumns(bool enabled) {
	if (enabled == this->wheelColumns) return;
	this->wheelColumns = enabled;
	gWheelColumns.store(enabled);

	auto* tracker = InputMaskTracker::instance();
	if (enabled) tracker->acquireHook();
	else tracker->releaseHook();
}

bool SuperDragManager::startWorker() {
	if (this->workerRunning) return true;

	gWorkerId.store(0);
	gWorkerStop.store(false);
	this->worker = std::thread(&SuperDragManager::workerMain);

	this->workerRunning = true;
	return true;
}

void SuperDragManager::stopWorker() {
	if (!this->workerRunning) return;

	gWorkerStop.store(true);
	auto id = gWorkerId.exchange(0);
	if (id != 0) PostThreadMessageW(id, WM_QUIT, 0, 0);

	this->worker.join();
	this->workerRunning = false;
}

void SuperDragManager::workerMain() {
	MSG msg {};
	PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	prioritizeInputThread(THREAD_PRIORITY_HIGHEST);
	gWorkerId.store(GetCurrentThreadId());
	if (gWorkerStop.load()) return;

	auto drag = Drag();

	for (;;) {
		auto timeout = drag.dirty ? RETRY_MS : INFINITE;
		auto wait = MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

		if (wait == WAIT_TIMEOUT) {
			updateDrag(drag, unpack(gCursor.load()), false);
			continue;
		}

		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
			switch (msg.message) {
			case WM_QUIT: finishDrag(drag); return;
			case WM_SD_BEGIN_MOVE:
			case WM_SD_BEGIN_RESIZE:
				finishDrag(drag);
				beginDrag(
				    drag,
				    reinterpret_cast<HWND>(msg.wParam), // NOLINT(performance-no-int-to-ptr)
				    msg.message == WM_SD_BEGIN_RESIZE,
				    unpack(static_cast<uint64_t>(msg.lParam))
				);
				break;
			case WM_SD_MOVE:
				gMovePending.store(false);
				updateDrag(drag, unpack(gCursor.load()), false);
				break;
			case WM_SD_END:
				updateDrag(drag, unpack(static_cast<uint64_t>(msg.lParam)), true);
				finishDrag(drag);
				break;
			default: DispatchMessageW(&msg); break;
			}
		}
	}
}

bool SuperDragManager::onMouseHook(WPARAM message, const MSLLHOOKSTRUCT* info) {
	auto& hook = gHook;

	if (hook.active) {
		if (message == WM_MOUSEMOVE) {
			gCursor.store(pack(info->pt));
			if (!gMovePending.exchange(true) && !postWorker(WM_SD_MOVE, 0, 0)) gMovePending.store(false);
			return false;
		}

		if (message == (hook.resize ? WM_RBUTTONUP : WM_LBUTTONUP)) {
			hook.active = false;
			postWorker(WM_SD_END, 0, static_cast<LPARAM>(pack(info->pt)));
			return true;
		}

		return message == (hook.resize ? WM_RBUTTONDOWN : WM_LBUTTONDOWN);
	}

	if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
		return SuperDragManager::onWheel(message, info);
	}

	auto resize = message == WM_RBUTTONDOWN;
	if (message != WM_LBUTTONDOWN && !resize) return false;
	if (!gEnabled.load(std::memory_order_relaxed) || !superAlone()) return false;

	auto* hwnd = dragTarget(info->pt);
	if (hwnd == nullptr) return false;

	gCursor.store(pack(info->pt));

	auto begin = resize ? WM_SD_BEGIN_RESIZE : WM_SD_BEGIN_MOVE;
	if (!postWorker(begin, reinterpret_cast<WPARAM>(hwnd), static_cast<LPARAM>(pack(info->pt)))) {
		return false;
	}

	hook.active = true;
	hook.resize = resize;
	// a mod+drag is a chord: releasing the mod afterwards must not open the Start menu (Super) or an app's
	// menu bar (Alt)
	if (gDragAlt.load(std::memory_order_relaxed)) hotkeys::KeyboardHook::maskAlt();
	else hotkeys::KeyboardHook::noteSuperChord();
	return true;
}

bool SuperDragManager::onWheel(WPARAM message, const MSLLHOOKSTRUCT* info) {
	if (!gWheelColumns.load(std::memory_order_relaxed)) return false;
	if (!isDown(VK_LWIN) && !isDown(VK_RWIN)) return false;
	if (isDown(VK_CONTROL) || isDown(VK_MENU)) return false;

	auto horizontal = message == WM_MOUSEHWHEEL;
	if (horizontal == isDown(VK_SHIFT)) return false;

	auto delta = static_cast<qint32>(static_cast<SHORT>(HIWORD(info->mouseData)));
	auto step = horizontal ? delta : -delta;
	auto& hook = gHook;

	if (info->time - hook.wheelTime > WHEEL_RESET_MS || (hook.wheel > 0) != (step > 0)) {
		hook.wheel = 0;
	}

	hook.wheelTime = info->time;
	hook.wheel += step;
	auto columns = hook.wheel / WHEEL_DELTA;

	if (columns != 0) {
		hook.wheel -= columns * WHEEL_DELTA;
		if (auto* gui = gGuiWindow.load()) {
			PostMessageW(
			    gui,
			    WM_SD_WHEEL,
			    static_cast<WPARAM>(static_cast<INT_PTR>(columns)),
			    static_cast<LPARAM>(pack(info->pt))
			);
		}
	}

	hotkeys::KeyboardHook::maskSuper();
	return true;
}

void SuperDragManager::onHookStopped() {
	if (!gHook.active) return;
	gHook.active = false;
	postWorker(WM_SD_END, 0, static_cast<LPARAM>(gCursor.load()));
}

LRESULT CALLBACK
SuperDragManager::messageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	auto* target = reinterpret_cast<HWND>(wParam); // NOLINT(performance-no-int-to-ptr)

	switch (msg) {
	case WM_SD_STARTED: return SuperDragManager::instance()->onStarted(target, lParam);
	case WM_SD_TILED_RESIZE: SuperDragManager::instance()->onTiledResize(); return 0;
	case WM_SD_ENDED: SuperDragManager::instance()->onEnded(target, lParam); return 0;
	case WM_SD_WHEEL:
		if (auto* tiling = TilingManager::active()) {
			tiling->focusColumnAt(
			    unpack(static_cast<uint64_t>(lParam)),
			    static_cast<qint32>(static_cast<INT_PTR>(wParam))
			);
		}
		return 0;
	default: return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}

LRESULT SuperDragManager::onStarted(HWND hwnd, LPARAM flags) {
	this->finishGuiDrag(nullptr);

	auto* tiling = TilingManager::active();
	if (tiling == nullptr) return 0;

	auto* tracker = WindowTracker::instance();
	this->guiDrag.hwnd = hwnd;

	if ((flags & FLAG_RESIZE) != 0) {
		auto* window = tracker->windowFor(hwnd);
		auto left = (flags & FLAG_LEFT) != 0;
		auto top = (flags & FLAG_TOP) != 0;

		if (window != nullptr && tiling->beginEdgeDrag(window, left, top)) {
			this->guiDrag.tiledResize = true;
			return STARTED_TILED_RESIZE;
		}
	}

	tracker->noteMoveSize(hwnd, true);
	this->guiDrag.notified = true;
	return STARTED_WATCHED;
}

void SuperDragManager::onTiledResize() {
	gTiledPending.store(false);
	if (!this->guiDrag.tiledResize) return;

	auto delta = unpack(gTiledDelta.load());
	if (auto* tiling = TilingManager::active()) tiling->edgeDrag(QPoint(delta.x, delta.y));
}

void SuperDragManager::onEnded(HWND hwnd, LPARAM delta) {
	if (hwnd == nullptr || hwnd != this->guiDrag.hwnd) return;

	auto point = unpack(static_cast<uint64_t>(delta));
	this->finishGuiDrag(&point);
}

void SuperDragManager::finishGuiDrag(const POINT* delta) {
	auto drag = this->guiDrag;
	this->guiDrag = GuiDrag();
	if (drag.hwnd == nullptr) return;

	if (drag.tiledResize) {
		if (auto* tiling = TilingManager::active()) {
			if (delta != nullptr) tiling->edgeDrag(QPoint(delta->x, delta->y));
			tiling->endEdgeDrag();
		}
	} else if (drag.notified) {
		WindowTracker::instance()->noteMoveSize(drag.hwnd, false);
	}
}

SuperDrag::SuperDrag(QObject* parent): QObject(parent) {
	QObject::connect(
	    SuperDragManager::instance(),
	    &SuperDragManager::enabledChanged,
	    this,
	    &SuperDrag::enabledChanged
	);
}

bool SuperDrag::enabled() const { return SuperDragManager::instance()->enabled(); }
void SuperDrag::setEnabled(bool enabled) { SuperDragManager::instance()->setEnabled(enabled); }

QString SuperDrag::modifier() const { return gDragAlt.load() ? QStringLiteral("alt") : QStringLiteral("super"); }
void SuperDrag::setModifier(const QString& modifier) {
	auto alt = modifier.trimmed().compare("alt", Qt::CaseInsensitive) == 0;
	if (alt == gDragAlt.load()) return;
	gDragAlt.store(alt);
	emit this->modifierChanged();
}

} // namespace qs::windows
