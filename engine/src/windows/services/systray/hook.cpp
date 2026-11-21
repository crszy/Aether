#include "hook.hpp"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <qt_windows.h>
#include <shellapi.h>

#include <qimage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <quuid.h>

#include "../../../core/logcat.hpp"
#include "../../util.hpp"

namespace qs::windows::services::systray {

namespace {

QS_LOGGING_CATEGORY(logTrayHook, "quickshell.windows.systray", QtWarningMsg);

constexpr const wchar_t* TRAY_CLASS = L"Shell_TrayWnd";
constexpr const wchar_t* NOTIFY_CLASS = L"TrayNotifyWnd";
constexpr const wchar_t* HOOK_TITLE = L"Quickshell tray hook";
constexpr UINT PROBE_UID = 0x51535459;

constexpr ULONG_PTR COPYDATA_NOTIFYICON = 1;
constexpr DWORD NOTIFYICON_SIGNATURE = 0x34753423;

constexpr UINT_PTR RAISE_TIMER = 1;
constexpr UINT_PTR ANNOUNCE_TIMER = 2;
constexpr UINT RAISE_INTERVAL_MS = 100;
constexpr UINT IDLE_RAISE_INTERVAL_MS = 1000;
constexpr ULONGLONG ALERT_MS = 10000;
constexpr ULONGLONG PROPS_SYNC_MS = 1000;
constexpr ULONGLONG IDLE_PROPS_SYNC_MS = 5000;
constexpr UINT ANNOUNCE_DELAY_MS = 1500;
constexpr UINT FORWARD_TIMEOUT_MS = 4000;
constexpr ULONGLONG READD_GRACE_MS = 30000;
constexpr ULONGLONG BRIEF_FRONT_MS = 8000;

struct NotifyIconWire {
	DWORD cbSize;
	DWORD hWnd;
	UINT uID;
	UINT uFlags;
	UINT uCallbackMessage;
	DWORD hIcon;
	WCHAR szTip[128];
	DWORD dwState;
	DWORD dwStateMask;
	WCHAR szInfo[256];
	UINT uVersion;
	WCHAR szInfoTitle[64];
	DWORD dwInfoFlags;
	GUID guidItem;
	DWORD hBalloonIcon;
};

static_assert(sizeof(NotifyIconWire) == 956, "the 32 bit NOTIFYICONDATAW is 956 bytes");

struct TrayDataWire {
	DWORD signature;
	DWORD message;
	NotifyIconWire nid;
};

constexpr size_t MIN_TRAY_DATA = offsetof(TrayDataWire, nid) + offsetof(NotifyIconWire, szTip);

template <typename T>
T handleFromWire(DWORD value) {
	// NOLINTNEXTLINE(performance-no-int-to-ptr)
	return reinterpret_cast<T>(static_cast<LONG_PTR>(static_cast<LONG>(value)));
}

bool processIsElevated() {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

	TOKEN_ELEVATION elevation {};
	DWORD size = 0;
	auto ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
	CloseHandle(token);

	return ok && elevation.TokenIsElevated != 0;
}

// Elevated *and* the session has a split UAC token, so ordinary apps run non-elevated and UIPI would drop their
// Shell_NotifyIcon messages to our window. With a single full token (the built-in Administrator account, or UAC
// off) every app runs at the same level as the shell, nothing is dropped, and hooking the tray is safe.
bool elevatedOverNormalApps() {
	if (!processIsElevated()) return false;
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return true;
	TOKEN_ELEVATION_TYPE type = TokenElevationTypeDefault;
	DWORD size = 0;
	auto ok = GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &size);
	CloseHandle(token);
	return !ok || type == TokenElevationTypeFull;
}

class HookWindow {
public:
	explicit HookWindow(TrayIconSink sink, std::function<void()> missed)
	    : sink(std::move(sink))
	    , missed(std::move(missed)) {}
	~HookWindow() = default;
	Q_DISABLE_COPY_MOVE(HookWindow);

	bool create();
	void probeTitle();
	void announce();

	HWND hwnd = nullptr;

private:
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);

	LRESULT onCopyData(WPARAM wParam, LPARAM lParam);
	void onTaskbarCreated();
	void announceQueued();
	std::optional<LRESULT> forward(UINT msg, WPARAM wParam, LPARAM lParam);
	HWND explorerWindow();
	void keepFirst(bool report = true);
	void stepBack(HWND first);
	void syncGeometry();
	void mirrorProps();
	void stayAlert();
	void paceRaises();

	TrayIconSink sink;
	std::function<void()> missed;
	HWND notifyHwnd = nullptr;
	HWND explorer = nullptr;
	RECT lastRect {};
	RECT lastNotifyRect {};
	UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
	ULONGLONG readdGraceUntil = 0;
	ULONGLONG propsSyncedAt = 0;
	ULONGLONG alertUntil = 0;
	UINT raiseInterval = 0;
	std::vector<std::wstring> mirrored;
	bool probing = false;
	bool probeSeen = false;
};

std::mutex gMutex;                  // NOLINT
std::thread gThread;                // NOLINT
std::atomic<HWND> gHwnd = nullptr;  // NOLINT
std::atomic<bool> gStopping = false; // NOLINT
std::atomic<bool> gStarted = false;  // NOLINT
std::atomic<bool> gBrief = false;         // NOLINT
std::atomic<ULONGLONG> gFrontUntil = 0;   // NOLINT

void stayInFront(ULONGLONG ms) {
	auto until = GetTickCount64() + ms;
	auto current = gFrontUntil.load();
	while (current < until && !gFrontUntil.compare_exchange_weak(current, until)) {}
}

bool wantFront() { return !gBrief.load() || GetTickCount64() < gFrontUntil.load(); }

std::mutex gAnnounceMutex;          // NOLINT
std::vector<HWND> gAnnounceQueue;   // NOLINT

DWORD explorerProcessId() {
	auto* taskbar = explorerTaskbarWindow();
	DWORD pid = 0;
	if (taskbar != nullptr) GetWindowThreadProcessId(taskbar, &pid);
	return pid;
}

bool ownedBy(HWND window, DWORD pid) {
	if (pid == 0) return false;
	DWORD owner = 0;
	GetWindowThreadProcessId(window, &owner);
	return owner == pid;
}

UINT announceMessage() {
	static const UINT message = RegisterWindowMessageW(L"QuickshellTrayHookAnnounce"); // NOLINT
	return message;
}

TrayIconMessage decode(const TrayDataWire& wire) {
	const auto& nid = wire.nid;

	TrayIconMessage message;
	message.message = wire.message;
	message.hwnd = handleFromWire<HWND>(nid.hWnd);
	message.uid = nid.uID;
	message.flags = nid.uFlags;
	message.callbackMessage = nid.uCallbackMessage;
	message.state = nid.dwState;
	message.stateMask = nid.dwStateMask;

	if ((nid.uFlags & NIF_ICON) != 0) {
		auto* icon = handleFromWire<HICON>(nid.hIcon);
		message.icon = imageFromIcon(icon);
	}

	if ((nid.uFlags & NIF_TIP) != 0) {
		auto length = std::find(std::begin(nid.szTip), std::end(nid.szTip), L'\0') - std::begin(nid.szTip);
		message.tip = QString::fromWCharArray(nid.szTip, length);
	}

	message.version = nid.uVersion;
	if ((nid.uFlags & NIF_GUID) != 0) message.guid = QUuid(nid.guidItem);

	return message;
}

bool HookWindow::create() {
	auto* instance = GetModuleHandleW(nullptr);

	WNDCLASSEXW trayClass {};
	trayClass.cbSize = sizeof(trayClass);
	trayClass.lpfnWndProc = &HookWindow::wndProc;
	trayClass.hInstance = instance;
	trayClass.lpszClassName = TRAY_CLASS;

	WNDCLASSEXW notifyClass {};
	notifyClass.cbSize = sizeof(notifyClass);
	notifyClass.lpfnWndProc = &DefWindowProcW;
	notifyClass.hInstance = instance;
	notifyClass.lpszClassName = NOTIFY_CLASS;

	if (RegisterClassExW(&trayClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
		qCWarning(logTrayHook) << "Could not register the tray window class:" << GetLastError();
		return false;
	}

	RegisterClassExW(&notifyClass);

	this->hwnd = CreateWindowExW(
	    WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
	    TRAY_CLASS,
	    L"",
	    WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
	    0,
	    0,
	    0,
	    0,
	    nullptr,
	    nullptr,
	    instance,
	    this
	);

	if (this->hwnd == nullptr) {
		qCWarning(logTrayHook) << "Could not create the tray window:" << GetLastError();
		return false;
	}

	SetLayeredWindowAttributes(this->hwnd, 0, 0, LWA_ALPHA);
	SetPropW(this->hwnd, TRAY_HOOK_PROP, reinterpret_cast<HANDLE>(1)); // NOLINT

	this->notifyHwnd = CreateWindowExW(
	    0,
	    NOTIFY_CLASS,
	    L"",
	    WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
	    0,
	    0,
	    0,
	    0,
	    this->hwnd,
	    nullptr,
	    instance,
	    nullptr
	);

	this->syncGeometry();
	this->keepFirst(false);
	this->stayAlert();

	return true;
}

void HookWindow::probeTitle() {
	SetWindowTextW(this->hwnd, HOOK_TITLE);

	NOTIFYICONDATAW nid {};
	nid.cbSize = sizeof(nid);
	nid.hWnd = this->hwnd;
	nid.uID = PROBE_UID;
	nid.uFlags = NIF_STATE;

	this->probing = true;
	this->probeSeen = false;
	Shell_NotifyIconW(NIM_MODIFY, &nid);
	this->probing = false;

	if (this->probeSeen) return;

	qCInfo(logTrayHook) << "Shell_NotifyIcon didn't reach the tray hook with a window title of its"
	                    << "own, using explorer's empty one";
	SetWindowTextW(this->hwnd, L"");
}

void HookWindow::announce() {
	this->readdGraceUntil = GetTickCount64() + READD_GRACE_MS;
	stayInFront(BRIEF_FRONT_MS);
	this->stayAlert();
	this->keepFirst(false);

	struct Broadcast {
		UINT message;
		DWORD explorer;
	} broadcast {.message = this->taskbarCreated, .explorer = explorerProcessId()};

	EnumWindows(
	    [](HWND window, LPARAM param) -> BOOL {
		    auto* broadcast = reinterpret_cast<Broadcast*>(param); // NOLINT
		    if (!isOwnProcessWindow(window) && !ownedBy(window, broadcast->explorer)) {
			    SendNotifyMessageW(window, broadcast->message, 0, 0);
		    }
		    return TRUE;
	    },
	    reinterpret_cast<LPARAM>(&broadcast)
	);

	qCDebug(logTrayHook) << "Sent TaskbarCreated";
}

void HookWindow::announceQueued() {
	std::vector<HWND> owners;
	{
		auto lock = std::lock_guard(gAnnounceMutex);
		owners.swap(gAnnounceQueue);
	}

	std::ranges::sort(owners);
	auto [first, last] = std::ranges::unique(owners);
	owners.erase(first, last);
	if (owners.empty()) return;

	stayInFront(BRIEF_FRONT_MS);
	this->stayAlert();
	this->keepFirst();
	this->readdGraceUntil = GetTickCount64() + READD_GRACE_MS;

	auto explorer = explorerProcessId();
	auto sent = 0;
	for (auto* owner: owners) {
		if (IsWindow(owner) == 0 || isOwnProcessWindow(owner) || ownedBy(owner, explorer)) continue;
		SendNotifyMessageW(owner, this->taskbarCreated, 0, 0);
		sent++;
	}

	qCDebug(logTrayHook) << "Sent TaskbarCreated to" << sent << "icon owners";
}

LRESULT CALLBACK HookWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_NCCREATE) {
		auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam); // NOLINT
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	auto* self = reinterpret_cast<HookWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); // NOLINT
	if (self == nullptr || self->hwnd != hwnd) return DefWindowProcW(hwnd, msg, wParam, lParam);

	return self->handle(msg, wParam, lParam);
}

LRESULT HookWindow::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_COPYDATA: return this->onCopyData(wParam, lParam);
	case WM_TIMER:
		if (wParam == RAISE_TIMER) {
			if (trayHookYielding()) this->stayAlert();
			this->keepFirst();
			this->syncGeometry();
			this->paceRaises();
		} else if (wParam == ANNOUNCE_TIMER) {
			KillTimer(this->hwnd, ANNOUNCE_TIMER);
			this->announce();
		}
		return 0;
	case WM_WINDOWPOSCHANGING: {
		auto* pos = reinterpret_cast<WINDOWPOS*>(lParam); // NOLINT
		pos->flags &= ~SWP_SHOWWINDOW;
		break;
	}
	case WM_WINDOWPOSCHANGED:
		if (IsWindowVisible(this->hwnd)) {
			SetWindowLongPtrW(
			    this->hwnd,
			    GWL_STYLE,
			    GetWindowLongPtrW(this->hwnd, GWL_STYLE) & ~static_cast<LONG_PTR>(WS_VISIBLE)
			);
		}
		break;
	case WM_CLOSE:
		if (gStopping.load()) {
			DestroyWindow(this->hwnd);
			return 0;
		}

		return this->forward(msg, wParam, lParam).value_or(0);
	case WM_DESTROY:
		KillTimer(this->hwnd, RAISE_TIMER);
		KillTimer(this->hwnd, ANNOUNCE_TIMER);
		RemovePropW(this->hwnd, TRAY_HOOK_PROP);
		PostQuitMessage(0);
		return 0;
	default: break;
	}

	if (msg == this->taskbarCreated) {
		this->onTaskbarCreated();
		return 0;
	}

	if (msg == announceMessage()) {
		this->announceQueued();
		return 0;
	}

	if (msg == WM_COMMAND || msg >= WM_USER) {
		return this->forward(msg, wParam, lParam).value_or(0);
	}

	return DefWindowProcW(this->hwnd, msg, wParam, lParam);
}

LRESULT HookWindow::onCopyData(WPARAM wParam, LPARAM lParam) {
	auto* data = reinterpret_cast<COPYDATASTRUCT*>(lParam); // NOLINT

	if (data == nullptr || data->dwData != COPYDATA_NOTIFYICON || data->lpData == nullptr
	    || data->cbData < MIN_TRAY_DATA)
	{
		return this->forward(WM_COPYDATA, wParam, lParam).value_or(0);
	}

	TrayDataWire wire {};
	std::memcpy(&wire, data->lpData, std::min<size_t>(data->cbData, sizeof(wire)));

	if (wire.signature != NOTIFYICON_SIGNATURE) {
		return this->forward(WM_COPYDATA, wParam, lParam).value_or(0);
	}

	auto message = decode(wire);

	if (message.uid == PROBE_UID && isTrayHookWindow(message.hwnd)) {
		if (this->probing && message.hwnd == this->hwnd) this->probeSeen = true;
		return FALSE;
	}

	LRESULT answer = FALSE;

	if (this->explorerWindow() == nullptr) {
		answer = wire.message != NIM_SETVERSION || wire.nid.uVersion <= NOTIFYICON_VERSION_4;
	} else {
		answer = this->forward(WM_COPYDATA, wParam, lParam).value_or(FALSE);

		if (wire.message == NIM_ADD && answer == FALSE && GetTickCount64() < this->readdGraceUntil) {
			std::vector<char> copy(data->cbData);
			std::memcpy(copy.data(), data->lpData, data->cbData);

			DWORD modify = NIM_MODIFY;
			std::memcpy(copy.data() + offsetof(TrayDataWire, message), &modify, sizeof(modify));

			auto retry = *data;
			retry.lpData = copy.data();

			answer = this->forward(WM_COPYDATA, wParam, reinterpret_cast<LPARAM>(&retry)).value_or(FALSE);
		}
	}

	this->sink(std::move(message));
	return answer;
}

void HookWindow::onTaskbarCreated() {
	auto* previous = this->explorer;
	this->explorer = explorerTaskbarWindow();
	if (this->explorer == previous || this->explorer == nullptr) return;

	qCInfo(logTrayHook) << "Explorer restarted, asking apps for their tray icons again";
	stayInFront(ANNOUNCE_DELAY_MS + BRIEF_FRONT_MS);
	this->stayAlert();
	this->propsSyncedAt = 0;
	this->lastRect = {};
	this->lastNotifyRect = {};
	this->keepFirst();
	SetTimer(this->hwnd, ANNOUNCE_TIMER, ANNOUNCE_DELAY_MS, nullptr);
}

std::optional<LRESULT> HookWindow::forward(UINT msg, WPARAM wParam, LPARAM lParam) {
	auto* target = this->explorerWindow();
	if (target == nullptr) return std::nullopt;

	auto kind = InSendMessageEx(nullptr) & (ISMEX_SEND | ISMEX_NOTIFY | ISMEX_CALLBACK);

	if (kind == ISMEX_NOSEND) {
		PostMessageW(target, msg, wParam, lParam);
		return 0;
	}

	if ((kind & ISMEX_NOTIFY) != 0) {
		SendNotifyMessageW(target, msg, wParam, lParam);
		return 0;
	}

	DWORD_PTR result = 0;
	if (SendMessageTimeoutW(
	        target,
	        msg,
	        wParam,
	        lParam,
	        SMTO_NORMAL | SMTO_ABORTIFHUNG,
	        FORWARD_TIMEOUT_MS,
	        &result
	    )
	    == 0)
	{
		qCDebug(logTrayHook) << "Explorer didn't answer message" << msg << GetLastError();
		return std::nullopt;
	}

	return static_cast<LRESULT>(result);
}

HWND HookWindow::explorerWindow() {
	if (this->explorer == nullptr || !IsWindow(this->explorer)) {
		this->explorer = explorerTaskbarWindow();
	}

	return this->explorer;
}

void HookWindow::keepFirst(bool report) {
	if (trayHookYielding()) return;
	auto* first = FindWindowW(TRAY_CLASS, nullptr);

	if (!wantFront()) {
		this->stepBack(first);
		return;
	}

	if (first == this->hwnd || first == nullptr) return;

	if (isTrayHookWindow(first)) return;

	qCDebug(logTrayHook) << "Explorer's tray window went above ours, raising";

	this->stayAlert();
	SetWindowPos(
	    this->hwnd,
	    HWND_TOPMOST,
	    0,
	    0,
	    0,
	    0,
	    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER
	);

	if (report && this->missed) this->missed();
}

void HookWindow::stepBack(HWND first) {
	if (first != this->hwnd) return;

	auto* target = this->explorerWindow();
	if (target == nullptr) return;

	SetWindowPos(
	    this->hwnd,
	    target,
	    0,
	    0,
	    0,
	    0,
	    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER
	);
}

void HookWindow::mirrorProps() {
	auto* target = this->explorerWindow();
	if (target == nullptr) return;

	struct Found {
		std::vector<std::pair<std::wstring, HANDLE>> props;
	} found;

	EnumPropsExW(
	    target,
	    [](HWND /*hwnd*/, LPWSTR name, HANDLE value, ULONG_PTR param) -> BOOL {
		    auto* found = reinterpret_cast<Found*>(param); // NOLINT(performance-no-int-to-ptr)
		    auto key = IS_INTRESOURCE(name)
		                 ? L"#" + std::to_wstring(reinterpret_cast<ULONG_PTR>(name)) // NOLINT
		                 : std::wstring(name);
		    found->props.emplace_back(std::move(key), value);
		    return TRUE;
	    },
	    reinterpret_cast<LPARAM>(&found)
	);

	auto nameOf = [](const std::wstring& key) {
		return key.starts_with(L'#') ? MAKEINTATOM(std::stoul(key.substr(1))) : key.c_str();
	};

	std::vector<std::wstring> current;
	for (const auto& [key, value]: found.props) {
		if (key == TRAY_HOOK_PROP) continue;
		SetPropW(this->hwnd, nameOf(key), value);
		current.push_back(key);
	}

	for (const auto& key: this->mirrored) {
		if (std::ranges::find(current, key) == current.end()) RemovePropW(this->hwnd, nameOf(key));
	}

	this->mirrored = std::move(current);
	this->propsSyncedAt = GetTickCount64();
}

void HookWindow::stayAlert() {
	this->alertUntil = GetTickCount64() + ALERT_MS;
	if (this->raiseInterval == RAISE_INTERVAL_MS) return;

	this->raiseInterval = RAISE_INTERVAL_MS;
	SetTimer(this->hwnd, RAISE_TIMER, RAISE_INTERVAL_MS, nullptr);
}

void HookWindow::paceRaises() {
	auto interval = GetTickCount64() < this->alertUntil ? RAISE_INTERVAL_MS : IDLE_RAISE_INTERVAL_MS;
	if (interval == this->raiseInterval) return;

	this->raiseInterval = interval;
	SetTimer(this->hwnd, RAISE_TIMER, interval, nullptr);
}

void HookWindow::syncGeometry() {
	auto* target = this->explorerWindow();
	if (target == nullptr) return;

	auto now = GetTickCount64();
	auto propsEvery = now < this->alertUntil ? PROPS_SYNC_MS : IDLE_PROPS_SYNC_MS;
	if (now - this->propsSyncedAt >= propsEvery) this->mirrorProps();

	RECT rect {};
	if (!GetWindowRect(target, &rect)) return;

	if (!EqualRect(&rect, &this->lastRect)) {
		this->lastRect = rect;
		SetWindowPos(
		    this->hwnd,
		    nullptr,
		    rect.left,
		    rect.top,
		    rect.right - rect.left,
		    rect.bottom - rect.top,
		    SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER
		);
	}

	auto* notify = FindWindowExW(target, nullptr, NOTIFY_CLASS, nullptr);
	RECT notifyRect {};
	if (this->notifyHwnd == nullptr || notify == nullptr || !GetWindowRect(notify, &notifyRect)) {
		return;
	}

	OffsetRect(&notifyRect, -rect.left, -rect.top);

	if (!EqualRect(&notifyRect, &this->lastNotifyRect)) {
		this->lastNotifyRect = notifyRect;
		SetWindowPos(
		    this->notifyHwnd,
		    nullptr,
		    notifyRect.left,
		    notifyRect.top,
		    notifyRect.right - notifyRect.left,
		    notifyRect.bottom - notifyRect.top,
		    SWP_NOZORDER | SWP_NOACTIVATE
		);
	}
}

void runHook(TrayIconSink sink, std::function<void()> missed) {
	SetThreadDescription(GetCurrentThread(), L"qs tray hook");
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	stayInFront(BRIEF_FRONT_MS);

	HookWindow window(std::move(sink), std::move(missed));
	if (!window.create()) return;

	gHwnd.store(window.hwnd);

	if (gStopping.load()) {
		DestroyWindow(window.hwnd);
		gHwnd.store(nullptr);
		return;
	}

	window.probeTitle();
	window.announce();

	PostMessageW(window.hwnd, announceMessage(), 0, 0);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	gHwnd.store(nullptr);
}

} // namespace

void TrayHook::start(TrayIconSink sink, std::function<void()> missedTraffic, bool brief) {
	auto lock = std::lock_guard(gMutex);
	if (gThread.joinable()) return;

	if (elevatedOverNormalApps()) {
		qCWarning(logTrayHook) << "Running elevated: not hooking the system tray, so apps keep"
		                       << "reaching explorer's. Tray icons can't be clicked from the shell.";
		return;
	}

	gStopping.store(false);
	gStarted.store(true);
	gBrief.store(brief);
	gThread = std::thread(runHook, std::move(sink), std::move(missedTraffic));
}

void TrayHook::stop() {
	auto lock = std::lock_guard(gMutex);
	if (!gThread.joinable()) return;

	gStopping.store(true);
	gStarted.store(false);

	auto* hwnd = gHwnd.load();
	if (hwnd != nullptr) PostMessageW(hwnd, WM_CLOSE, 0, 0);

	if (WaitForSingleObject(gThread.native_handle(), 1000) == WAIT_OBJECT_0) gThread.join();
	else gThread.detach();
}

void TrayHook::announceTo(std::vector<HWND> owners) {
	if (owners.empty() || !gStarted.load()) return;

	{
		auto lock = std::lock_guard(gAnnounceMutex);
		if (gAnnounceQueue.size() > 1024) gAnnounceQueue.clear();
		gAnnounceQueue.insert(gAnnounceQueue.end(), owners.begin(), owners.end());
	}

	auto* hwnd = gHwnd.load();
	if (hwnd != nullptr) PostMessageW(hwnd, announceMessage(), 0, 0);
}

QImage imageFromIcon(HICON icon) {
	ICONINFO info {};
	if (icon == nullptr || !GetIconInfo(icon, &info)) return {};
	if (info.hbmColor != nullptr) DeleteObject(info.hbmColor);
	if (info.hbmMask != nullptr) DeleteObject(info.hbmMask);
	return QImage::fromHICON(icon);
}

} // namespace qs::windows::services::systray
