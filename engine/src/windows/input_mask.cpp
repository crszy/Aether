#include "input_mask.hpp"
#include <atomic>
#include <cmath>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <qcoreapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qpoint.h>
#include <qregion.h>
#include <qtimer.h>
#include <qwindow.h>
#include <windowsx.h>

#include "desktop_drop.hpp"
#include "super_drag.hpp"
#include "util.hpp"

namespace qs::windows {

namespace {

Q_LOGGING_CATEGORY(logInputMask, "quickshell.windows.inputmask", QtWarningMsg);

constexpr UINT WM_QS_CURSOR_MOVED = WM_APP + 1;
constexpr UINT WM_QS_BUTTON_PRESSED = WM_APP + 2;
constexpr UINT WM_QS_HOOK_FAILED = WM_APP + 3;
constexpr UINT WM_QS_MONITOR_CHANGED = WM_APP + 4;
constexpr auto MESSAGE_WINDOW_CLASS = L"QuickshellInputMaskTracker";
constexpr int CURSOR_INTERVAL_MS = 33;

struct MaskedWindow {
	HWND hwnd = nullptr;
	QRegion region;
	qreal dpr = 1;

	[[nodiscard]] bool operator==(const MaskedWindow& other) const = default;
};

using MaskList = std::vector<MaskedWindow>;

std::atomic<LONG> cursorX = 0;                       // NOLINT
std::atomic<LONG> cursorY = 0;                       // NOLINT
std::atomic<bool> wakePending = false;               // NOLINT
std::atomic<HWND> hookTarget = nullptr;              // NOLINT
std::atomic<DWORD> hookThreadId = 0;                 // NOLINT
std::atomic<bool> hookStopRequested = false;         // NOLINT
std::atomic<int> buttonWatchers = 0;                 // NOLINT
std::atomic<bool> cursorWatched = false;             // NOLINT
std::atomic<DWORD> lastCursorWake = 0;               // NOLINT
std::atomic<bool> monitorWatched = false;            // NOLINT
std::atomic<LONG> monitorLeft = 0;                   // NOLINT
std::atomic<LONG> monitorTop = 0;                    // NOLINT
std::atomic<LONG> monitorRight = 0;                  // NOLINT
std::atomic<LONG> monitorBottom = 0;                 // NOLINT
std::atomic<std::shared_ptr<const MaskList>> gMasks; // NOLINT

constexpr DWORD LATE_INPUT_MS = 100;
constexpr int LATE_REPORT_INTERVAL_MS = 10000;
std::atomic<quint32> lateMouseEvents = 0; // NOLINT
std::atomic<quint32> lateKeyEvents = 0;   // NOLINT
std::atomic<DWORD> worstLateMs = 0;       // NOLINT

QPoint localPoint(POINT cursor, const RECT& rect, qreal dpr) {
	return QPoint(
	    static_cast<int>(std::floor((cursor.x - rect.left) / dpr)),
	    static_cast<int>(std::floor((cursor.y - rect.top) / dpr))
	);
}

bool masksNeedEvaluation(POINT cursor) {
	auto masks = gMasks.load(std::memory_order_acquire);
	if (masks == nullptr) return false;

	for (const auto& window: *masks) {
		if (!IsWindowVisible(window.hwnd)) continue;

		auto transparent = (GetWindowLongPtrW(window.hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0;

		if (window.region.isEmpty()) {
			if (!transparent) return true;
			continue;
		}

		RECT rect {};
		if (!GetWindowRect(window.hwnd, &rect) || !PtInRect(&rect, cursor)) continue;

		if (window.region.contains(localPoint(cursor, rect, window.dpr)) == transparent) return true;
	}

	return false;
}

bool cursorWakeDue() {
	if (!cursorWatched.load(std::memory_order_relaxed)) return false;

	auto now = GetTickCount();
	auto elapsed = now - lastCursorWake.load(std::memory_order_relaxed);
	if (elapsed < static_cast<DWORD>(CURSOR_INTERVAL_MS)) return false;

	lastCursorWake.store(now, std::memory_order_relaxed);
	return true;
}

void noteCursorMonitor(POINT cursor) {
	if (cursor.x >= monitorLeft.load(std::memory_order_relaxed)
	    && cursor.x < monitorRight.load(std::memory_order_relaxed)
	    && cursor.y >= monitorTop.load(std::memory_order_relaxed)
	    && cursor.y < monitorBottom.load(std::memory_order_relaxed))
	{
		return;
	}

	auto* monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
	MONITORINFO info {};
	info.cbSize = sizeof(info);
	if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) return;

	monitorLeft.store(info.rcMonitor.left, std::memory_order_relaxed);
	monitorTop.store(info.rcMonitor.top, std::memory_order_relaxed);
	monitorRight.store(info.rcMonitor.right, std::memory_order_relaxed);
	monitorBottom.store(info.rcMonitor.bottom, std::memory_order_relaxed);

	if (auto* target = hookTarget.load()) {
		PostMessageW(target, WM_QS_MONITOR_CHANGED, reinterpret_cast<WPARAM>(monitor), 0);
	}
}

void forgetCursorMonitor() {
	monitorLeft.store(0, std::memory_order_relaxed);
	monitorTop.store(0, std::memory_order_relaxed);
	monitorRight.store(0, std::memory_order_relaxed);
	monitorBottom.store(0, std::memory_order_relaxed);
}

void wakeTracker() {
	if (wakePending.exchange(true)) return;

	auto* target = hookTarget.load();
	if (target == nullptr || !PostMessageW(target, WM_QS_CURSOR_MOVED, 0, 0)) wakePending.store(false);
}

} // namespace

void noteHookDelay(bool keyboard, DWORD eventTime) {
	auto delay = GetTickCount() - eventTime;
	if (delay < LATE_INPUT_MS || delay > 60000) return;

	(keyboard ? lateKeyEvents : lateMouseEvents).fetch_add(1, std::memory_order_relaxed);

	auto worst = worstLateMs.load(std::memory_order_relaxed);
	while (delay > worst && !worstLateMs.compare_exchange_weak(worst, delay)) {}
}

InputMaskTracker* InputMaskTracker::instance() {
	static QPointer<InputMaskTracker> tracker; // NOLINT

	if (tracker.isNull()) {
		tracker = new InputMaskTracker(QCoreApplication::instance());
	}

	return tracker.data();
}

InputMaskTracker::InputMaskTracker(QObject* parent): QObject(parent) {
	WNDCLASSW wndClass {};
	wndClass.lpfnWndProc = &InputMaskTracker::messageWindowProc;
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
		qCWarning(logInputMask) << "Failed to create message window, input masks will be polled.";
	}

	this->pollTimer.setInterval(16);
	QObject::connect(&this->pollTimer, &QTimer::timeout, this, &InputMaskTracker::refresh);

	this->cursorTrailTimer.setSingleShot(true);
	this->cursorTrailTimer.setInterval(CURSOR_INTERVAL_MS);
	QObject::connect(
	    &this->cursorTrailTimer,
	    &QTimer::timeout,
	    this,
	    &InputMaskTracker::onCursorTrail
	);

	this->lateReportTimer.setInterval(LATE_REPORT_INTERVAL_MS);
	QObject::connect(&this->lateReportTimer, &QTimer::timeout, this, []() {
		auto mouse = lateMouseEvents.exchange(0);
		auto keys = lateKeyEvents.exchange(0);
		auto worst = worstLateMs.exchange(0);
		if (mouse == 0 && keys == 0) return;

		qCWarning(logInputMask).nospace()
		    << "Input reached the hooks late in the last " << LATE_REPORT_INTERVAL_MS / 1000 << " s: "
		    << mouse << " mouse and " << keys << " keyboard events over " << LATE_INPUT_MS
		    << " ms, the worst " << worst << " ms.";
	});
	this->lateReportTimer.start();
}

InputMaskTracker::~InputMaskTracker() {
	this->stopHook();
	gMasks.store(nullptr, std::memory_order_release);

	if (this->messageWindow != nullptr) {
		DestroyWindow(this->messageWindow);
		this->messageWindow = nullptr;
	}
}

void InputMaskTracker::setMask(QWindow* window, const QRegion& region) {
	if (window == nullptr) return;

	auto found = false;
	for (auto& entry: this->entries) {
		if (entry.window == window) {
			if (entry.region == region) return;

			entry.region = region;
			found = true;
			break;
		}
	}

	if (!found) this->entries.push_back(Entry {.window = window, .region = region});
	qCDebug(logInputMask) << "Mask for" << window << "set to" << region;

	this->publish();
	this->updateHookState();
	this->refresh();
}

void InputMaskTracker::remove(QWindow* window) {
	auto removed = this->entries.removeIf([window](const Entry& entry) {
		return entry.window == window || entry.window.isNull();
	});

	if (removed != 0 && window != nullptr) {
		setExStyleBits(hwndOf(window), WS_EX_TRANSPARENT, false);
	}

	this->publish();
	this->updateHookState();
}

void InputMaskTracker::refresh() {
	this->publish();

	POINT cursor {};
	if (!GetCursorPos(&cursor)) return;
	this->evaluate(cursor);
}

void InputMaskTracker::publish() {
	MaskList next;
	next.reserve(static_cast<size_t>(this->entries.size()));

	for (const auto& entry: this->entries) {
		auto* window = entry.window.data();
		auto* hwnd = hwndOf(window);
		if (hwnd == nullptr) continue;

		next.push_back({.hwnd = hwnd, .region = entry.region, .dpr = window->devicePixelRatio()});
	}

	auto current = gMasks.load(std::memory_order_acquire);
	if (current == nullptr ? next.empty() : *current == next) return;

	auto snapshot = next.empty() ? nullptr : std::make_shared<const MaskList>(std::move(next));
	gMasks.store(std::move(snapshot), std::memory_order_release);
}

void InputMaskTracker::acquireButtonEvents() {
	buttonWatchers.fetch_add(1);
	this->updateHookState();

	if (!this->hookRunning) {
		qCWarning(logInputMask) << "Mouse hook unavailable, mouse button presses can't be observed.";
	}
}

void InputMaskTracker::releaseButtonEvents() {
	if (buttonWatchers.load() <= 0) return;
	buttonWatchers.fetch_sub(1);
	this->updateHookState();
}

void InputMaskTracker::acquireMonitorEvents() {
	this->monitorWatchers++;
	forgetCursorMonitor();
	this->updateHookState();
}

void InputMaskTracker::releaseMonitorEvents() {
	if (this->monitorWatchers <= 0) return;
	this->monitorWatchers--;
	this->updateHookState();
}

void InputMaskTracker::resetCursorMonitor() { forgetCursorMonitor(); }

void InputMaskTracker::acquireCursorEvents() {
	this->cursorWatchers++;
	this->updateHookState();
}

void InputMaskTracker::releaseCursorEvents() {
	if (this->cursorWatchers <= 0) return;
	this->cursorWatchers--;
	this->updateHookState();
}

void InputMaskTracker::acquireHook() {
	this->hookHolders++;
	this->updateHookState();
}

void InputMaskTracker::releaseHook() {
	if (this->hookHolders <= 0) return;
	this->hookHolders--;
	this->updateHookState();
}

void InputMaskTracker::onButtonPressed(QPoint position, quint32 time) {
	emit this->buttonPressed(position, time);
}

void InputMaskTracker::onCursorMoved() {
	wakePending.store(false);
	POINT cursor {.x = cursorX.load(), .y = cursorY.load()};
	this->evaluate(cursor);
}

void InputMaskTracker::emitCursor(QPoint position) {
	this->lastCursor = position;
	emit this->cursorMoved(position);

	if (this->hookRunning && this->cursorWatchers > 0) this->cursorTrailTimer.start();
}

void InputMaskTracker::onCursorTrail() {
	if (!this->hookRunning || this->cursorWatchers <= 0) return;

	auto position = QPoint(cursorX.load(), cursorY.load());
	if (position != this->lastCursor) this->emitCursor(position);
}

void InputMaskTracker::evaluate(POINT cursor) {
	if (this->cursorWatchers > 0) this->emitCursor(QPoint(cursor.x, cursor.y));

	auto pruned = false;

	for (auto it = this->entries.begin(); it != this->entries.end();) {
		auto* window = it->window.data();

		if (window == nullptr) {
			it = this->entries.erase(it);
			pruned = true;
			continue;
		}

		auto* hwnd = hwndOf(window);

		if (hwnd == nullptr || !window->isVisible()) {
			++it;
			continue;
		}

		if (it->region.isEmpty()) {
			setExStyleBits(hwnd, WS_EX_TRANSPARENT, true);
			++it;
			continue;
		}

		RECT rect {};
		if (GetWindowRect(hwnd, &rect) && PtInRect(&rect, cursor)) {
			auto local = localPoint(cursor, rect, window->devicePixelRatio());
			auto through = !it->region.contains(local);
			qCDebug(logInputMask) << "Cursor" << cursor.x << cursor.y << "local" << local << "in" << window
			                      << (through ? "passes through" : "hits the mask");
			setExStyleBits(hwnd, WS_EX_TRANSPARENT, through);
		}

		++it;
	}

	if (pruned) this->publish();
}

void InputMaskTracker::updateHookState() {
	auto needed = !this->entries.isEmpty() || buttonWatchers.load() > 0 || this->cursorWatchers > 0
	           || this->monitorWatchers > 0 || this->hookHolders > 0;

	if (needed && !this->hookRunning && !this->hookFailed) {
		if (this->messageWindow == nullptr || !this->startHook()) {
			qCWarning(logInputMask) << "Mouse hook unavailable, polling the cursor instead.";
			this->hookFailed = true;
		}
	}

	cursorWatched.store(this->hookRunning && this->cursorWatchers > 0);
	monitorWatched.store(this->hookRunning && this->monitorWatchers > 0);
	if (this->cursorWatchers <= 0) this->cursorTrailTimer.stop();

	if (needed && this->hookFailed && (!this->entries.isEmpty() || this->cursorWatchers > 0)
	    && !this->pollTimer.isActive())
	{
		this->pollTimer.start();
	}

	if (!needed) {
		this->stopHook();
		this->pollTimer.stop();
	}
}

bool InputMaskTracker::startHook() {
	if (this->hookRunning) return true;

	this->hookGeneration++;
	hookTarget.store(this->messageWindow);
	hookThreadId.store(0);
	hookStopRequested.store(false);
	this->hookThread = std::thread(&InputMaskTracker::hookThreadMain, this->hookGeneration);

	this->hookRunning = true;
	qCDebug(logInputMask) << "Mouse hook thread started.";
	return true;
}

void InputMaskTracker::stopHook() {
	if (!this->hookRunning) return;

	hookStopRequested.store(true);
	auto threadId = hookThreadId.load();
	if (threadId != 0) PostThreadMessageW(threadId, WM_QUIT, 0, 0);

	this->hookThread.join();
	this->hookRunning = false;
	hookTarget.store(nullptr);
	cursorWatched.store(false);
	this->cursorTrailTimer.stop();
}

void InputMaskTracker::onHookFailed(DWORD error, quint32 generation) {
	if (!this->hookRunning || generation != this->hookGeneration) return;

	this->hookThread.join();
	this->hookRunning = false;
	this->hookFailed = true;
	hookTarget.store(nullptr);
	cursorWatched.store(false);
	this->cursorTrailTimer.stop();

	qCWarning(logInputMask).nospace()
	    << "Mouse hook unavailable (error " << error << "), polling the cursor instead. "
	    << "Mouse button presses and Super drags can't be observed.";

	this->updateHookState();
}

void InputMaskTracker::hookThreadMain(quint32 generation) {
	MSG msg {};
	PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
	hookThreadId.store(GetCurrentThreadId());
	if (hookStopRequested.load()) return;

	prioritizeInputThread(THREAD_PRIORITY_TIME_CRITICAL);

	auto* hook =
	    SetWindowsHookExW(WH_MOUSE_LL, &InputMaskTracker::mouseHookProc, GetModuleHandleW(nullptr), 0);

	if (hook == nullptr) {
		auto error = GetLastError();
		if (auto* target = hookTarget.load()) {
			PostMessageW(target, WM_QS_HOOK_FAILED, error, static_cast<LPARAM>(generation));
		}

		return;
	}

	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	UnhookWindowsHookEx(hook);
	SuperDragManager::onHookStopped();
}

LRESULT CALLBACK InputMaskTracker::mouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
	if (code == HC_ACTION) {
		auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam); // NOLINT(performance-no-int-to-ptr)
		noteHookDelay(false, info->time);
		cursorX.store(info->pt.x);
		cursorY.store(info->pt.y);

		if (monitorWatched.load(std::memory_order_relaxed)) noteCursorMonitor(info->pt);

		if (cursorWakeDue() || masksNeedEvaluation(info->pt)) wakeTracker();

		if (SuperDragManager::onMouseHook(wParam, info)) return 1;
		DesktopDropManager::onMouseHook(wParam, info);

		auto button = wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN
		           || wParam == WM_XBUTTONDOWN;

		if (button && buttonWatchers.load() > 0) {
			if (auto* target = hookTarget.load()) {
				PostMessageW(
				    target,
				    WM_QS_BUTTON_PRESSED,
				    static_cast<WPARAM>(info->time),
				    MAKELPARAM(static_cast<WORD>(info->pt.x), static_cast<WORD>(info->pt.y))
				);
			}
		}
	}

	return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK
InputMaskTracker::messageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_QS_CURSOR_MOVED) {
		InputMaskTracker::instance()->onCursorMoved();
		return 0;
	}

	if (msg == WM_QS_BUTTON_PRESSED) {
		auto position = QPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		InputMaskTracker::instance()->onButtonPressed(position, static_cast<quint32>(wParam));
		return 0;
	}

	if (msg == WM_QS_MONITOR_CHANGED) {
		emit InputMaskTracker::instance()->cursorMonitorChanged(static_cast<quintptr>(wParam));
		return 0;
	}

	if (msg == WM_QS_HOOK_FAILED) {
		InputMaskTracker::instance()->onHookFailed(
		    static_cast<DWORD>(wParam),
		    static_cast<quint32>(lParam)
		);
		return 0;
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace qs::windows
