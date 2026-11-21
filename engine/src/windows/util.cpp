#include "util.hpp"

#include <atomic>
#include <cmath>

#include <qbytearray.h>
#include <qguiapplication.h>
#include <qpointer.h>
#include <qpoint.h>
#include <qrect.h>
#include <qscreen.h>
#include <qscreen_platform.h>
#include <qthreadpool.h>
#include <qtypes.h>
#include <qwindow.h>

#include <dwmapi.h>
#include <objbase.h>

namespace qs::windows {

HMONITOR monitorForScreen(QScreen* screen) {
	if (screen == nullptr) return nullptr;
	auto* native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
	HMONITOR handle = native == nullptr ? nullptr : native->handle();

	// Qt keeps the handle from when it first saw the screen; after a display change Windows may have issued new
	// ones, and a stale handle makes APIs like Windows Graphics Capture fail with E_INVALIDARG. Keep it only while
	// Windows still lists it, else find the monitor again by the screen's position.
	struct Find {
		HMONITOR wanted;
		bool found;
	} find {handle, false};
	if (handle != nullptr) {
		EnumDisplayMonitors(
		    nullptr,
		    nullptr,
		    [](HMONITOR m, HDC, LPRECT, LPARAM data) -> BOOL {
			    auto* f = reinterpret_cast<Find*>(data); // NOLINT(performance-no-int-to-ptr)
			    if (m == f->wanted) {
				    f->found = true;
				    return FALSE;
			    }
			    return TRUE;
		    },
		    reinterpret_cast<LPARAM>(&find)
		);
		if (find.found) return handle;
	}

	auto geometry = screen->geometry();
	auto dpr = screen->devicePixelRatio();
	POINT centre {
	    geometry.x() + static_cast<LONG>(geometry.width() * dpr / 2),
	    geometry.y() + static_cast<LONG>(geometry.height() * dpr / 2)
	};
	return MonitorFromPoint(centre, MONITOR_DEFAULTTONEAREST);
}

QScreen* screenForMonitor(HMONITOR monitor) {
	if (monitor == nullptr) return nullptr;

	for (auto* screen: QGuiApplication::screens()) {
		if (monitorForScreen(screen) == monitor) return screen;
	}

	return nullptr;
}

namespace {
QPointer<QScreen> gFocusedScreen; // NOLINT
}

QScreen* focusedScreen() { return gFocusedScreen.data(); }
void setFocusedScreen(QScreen* screen) { gFocusedScreen = screen; }

MonitorRects monitorRects(HMONITOR monitor) {
	MonitorRects rects;
	if (monitor == nullptr) return rects;

	MONITORINFO info {};
	info.cbSize = sizeof(info);
	if (!GetMonitorInfoW(monitor, &info)) return rects;

	rects.monitor = toQRect(info.rcMonitor);
	rects.work = toQRect(info.rcWork);
	rects.valid = true;
	return rects;
}

QPoint ScreenMapper::toLogical(const QPoint& physical) const {
	auto delta = physical - this->physicalOrigin;
	return this->logicalOrigin
	     + QPoint(
	           static_cast<int>(std::lround(delta.x() / this->dpr)),
	           static_cast<int>(std::lround(delta.y() / this->dpr))
	       );
}

QRect ScreenMapper::toLogical(const QRect& physical) const {
	auto topLeft = this->toLogical(physical.topLeft());
	auto bottomRight = this->toLogical(physical.topLeft() + QPoint(physical.width(), physical.height()));
	return QRect(topLeft, QSize(bottomRight.x() - topLeft.x(), bottomRight.y() - topLeft.y()));
}

QPoint ScreenMapper::toPhysical(const QPoint& logical) const {
	auto delta = logical - this->logicalOrigin;
	return this->physicalOrigin
	     + QPoint(
	           static_cast<int>(std::lround(delta.x() * this->dpr)),
	           static_cast<int>(std::lround(delta.y() * this->dpr))
	       );
}

QRect ScreenMapper::toPhysical(const QRect& logical) const {
	auto topLeft = this->toPhysical(logical.topLeft());
	auto bottomRight = this->toPhysical(logical.topLeft() + QPoint(logical.width(), logical.height()));
	return QRect(topLeft, QSize(bottomRight.x() - topLeft.x(), bottomRight.y() - topLeft.y()));
}

qint32 ScreenMapper::toPhysical(qint32 logicalLength) const {
	return static_cast<qint32>(std::lround(logicalLength * this->dpr));
}

HWND hwndOf(const QWindow* window) {
	if (window == nullptr || window->handle() == nullptr) return nullptr;
	return reinterpret_cast<HWND>(window->winId()); // NOLINT(performance-no-int-to-ptr)
}

QRect toQRect(const RECT& rect) {
	return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

RECT toRECT(const QRect& rect) {
	return RECT {
	    .left = rect.left(),
	    .top = rect.top(),
	    .right = rect.left() + rect.width(),
	    .bottom = rect.top() + rect.height(),
	};
}

DWORD windowsBuild() {
	static const DWORD build = []() -> DWORD {
		using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
		auto* ntdll = GetModuleHandleW(L"ntdll.dll");
		auto rtlGetVersion = ntdll == nullptr
		                       ? nullptr
		                       : reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
		if (rtlGetVersion == nullptr) return 0;

		OSVERSIONINFOW info {};
		info.dwOSVersionInfoSize = sizeof(info);
		return rtlGetVersion(&info) == 0 ? info.dwBuildNumber : 0;
	}();

	return build;
}

void runLauncher(std::function<void()> task) {
	static auto* pool = [] {
		auto* pool = new QThreadPool();
		pool->setMaxThreadCount(2);
		pool->setExpiryTimeout(30000);
		return pool;
	}();

	pool->start([task = std::move(task)]() {
		auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		task();
		if (SUCCEEDED(com)) CoUninitialize();
	});
}

bool forceForegroundWindow(HWND hwnd) {
	if (hwnd == nullptr) return false;
	if (GetForegroundWindow() == hwnd) return true;
	if (SetForegroundWindow(hwnd)) return true;

	INPUT input {};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_MOVE;
	if (SendInput(1, &input, sizeof(input)) == 1 && SetForegroundWindow(hwnd)) return true;

	auto* foreground = GetForegroundWindow();
	auto foregroundThread =
	    foreground == nullptr ? 0 : GetWindowThreadProcessId(foreground, nullptr);
	auto ownThread = GetCurrentThreadId();

	if (foregroundThread == 0 || foregroundThread == ownThread) return false;
	if (IsHungAppWindow(foreground)) return false;

	if (!AttachThreadInput(ownThread, foregroundThread, TRUE)) return false;
	auto ok = SetForegroundWindow(hwnd) != FALSE;
	AttachThreadInput(ownThread, foregroundThread, FALSE);

	if (ok) {
		SetFocus(hwnd);
	}

	return ok;
}

bool isOwnProcessWindow(HWND hwnd) {
	if (hwnd == nullptr) return false;
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	return pid == GetCurrentProcessId();
}

namespace {

DWORD tokenIntegrity(HANDLE token) {
	DWORD size = 0;
	GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
	if (size == 0) return 0;

	auto buffer = QByteArray(static_cast<qsizetype>(size), '\0');
	auto* label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data());
	if (!GetTokenInformation(token, TokenIntegrityLevel, label, size, &size)) return 0;

	auto* sid = label->Label.Sid;
	auto count = *GetSidSubAuthorityCount(sid);
	if (count == 0) return 0;
	return *GetSidSubAuthority(sid, count - 1);
}

DWORD ownIntegrity() {
	static const DWORD level = []() -> DWORD {
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
			return SECURITY_MANDATORY_MEDIUM_RID;
		}
		auto level = tokenIntegrity(token);
		CloseHandle(token);
		return level == 0 ? SECURITY_MANDATORY_MEDIUM_RID : level;
	}();

	return level;
}

} // namespace

bool isMoreElevated(HWND hwnd) {
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	auto own = ownIntegrity();

	auto* process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (process == nullptr) return own < SECURITY_MANDATORY_HIGH_RID;

	HANDLE token = nullptr;
	auto opened = OpenProcessToken(process, TOKEN_QUERY, &token) != FALSE;
	CloseHandle(process);
	if (!opened) return own < SECURITY_MANDATORY_HIGH_RID;

	auto level = tokenIntegrity(token);
	CloseHandle(token);
	return level > own;
}

bool isTrayHookWindow(HWND hwnd) {
	return hwnd != nullptr && GetPropW(hwnd, TRAY_HOOK_PROP) != nullptr;
}

namespace {
std::atomic<int> gTrayHookYields = 0; // NOLINT
} // namespace

bool trayHookYielding() { return gTrayHookYields.load() > 0; }

TrayHookYield::TrayHookYield() {
	if (gTrayHookYields.fetch_add(1) != 0) return;

	auto* first = FindWindowW(L"Shell_TrayWnd", nullptr);
	if (!isTrayHookWindow(first)) return;

	auto* explorer = explorerTaskbarWindow();
	if (explorer == nullptr) return;

	SetWindowPos(first, explorer, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

TrayHookYield::~TrayHookYield() { gTrayHookYields.fetch_sub(1); }

HWND explorerTaskbarWindow() {
	HWND hwnd = nullptr;
	while ((hwnd = FindWindowExW(nullptr, hwnd, L"Shell_TrayWnd", nullptr)) != nullptr) {
		if (!isTrayHookWindow(hwnd)) return hwnd;
	}
	return nullptr;
}

void applyPanelDwmAttributes(HWND hwnd) {
	if (hwnd == nullptr) return;

	auto corner = static_cast<DWM_WINDOW_CORNER_PREFERENCE>(DWMWCP_DONOTROUND);
	DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

	COLORREF border = DWMWA_COLOR_NONE;
	DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));

	BOOL enabled = TRUE;
	DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &enabled, sizeof(enabled));
	DwmSetWindowAttribute(hwnd, DWMWA_EXCLUDED_FROM_PEEK, &enabled, sizeof(enabled));

	auto policy = static_cast<DWMNCRENDERINGPOLICY>(DWMNCRP_DISABLED);
	DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));

	markNonRude(hwnd);
}

void markNonRude(HWND hwnd) {
	SetPropW(hwnd, L"NonRudeHWND", reinterpret_cast<HANDLE>(TRUE)); // NOLINT
}

void setExStyleBits(HWND hwnd, LONG_PTR bits, bool enabled) {
	if (hwnd == nullptr) return;
	auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	auto newStyle = enabled ? (style | bits) : (style & ~bits);
	if (newStyle != style) SetWindowLongPtrW(hwnd, GWL_EXSTYLE, newStyle);
}

void prioritizeInputThread(int priority) {
	SetThreadPriority(GetCurrentThread(), priority);

	THREAD_POWER_THROTTLING_STATE state {};
	state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
	state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
	state.StateMask = 0;
	SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
}

void optOutOfProcessPowerThrottling() {
	PROCESS_POWER_THROTTLING_STATE state {};
	state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
	state.StateMask = 0;
	SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
}

} // namespace qs::windows
