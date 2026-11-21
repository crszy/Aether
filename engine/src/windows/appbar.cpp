#include "appbar.hpp"
#include <condition_variable>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <shellapi.h>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qrect.h>
#include <qtypes.h>

#include "util.hpp"

namespace qs::windows {

namespace {
Q_LOGGING_CATEGORY(logAppBar, "quickshell.windows.appbar", QtWarningMsg);

constexpr auto REGISTERED_KEY = L"Software\\Quickshell\\AppBars";

APPBARDATA appBarData(HWND hwnd) {
	APPBARDATA data {};
	data.cbSize = sizeof(data);
	data.hWnd = hwnd;
	data.uCallbackMessage = WinAppBar::callbackMessage();
	return data;
}

std::wstring registeredName(HWND hwnd) {
	wchar_t name[24] {};
	swprintf(name, std::size(name), L"%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hwnd)));
	return name;
}

void rememberRegistration(HWND hwnd) {
	DWORD pid = GetCurrentProcessId();
	RegSetKeyValueW(
	    HKEY_CURRENT_USER,
	    REGISTERED_KEY,
	    registeredName(hwnd).c_str(),
	    REG_DWORD,
	    &pid,
	    sizeof(pid)
	);
}

void forgetRegistration(HWND hwnd) {
	RegDeleteKeyValueW(HKEY_CURRENT_USER, REGISTERED_KEY, registeredName(hwnd).c_str());
}

// ---- without Explorer there is no AppBar server (ABM_NEW fails), so nobody keeps the work area: Windows keeps
// whatever an earlier shell left (here: 58 px at the bottom for a taskbar that is not there, so tiled windows
// stopped short of it). The shell then does what Explorer does for its taskbar: it sets each monitor's work area
// to the monitor minus what its panels reserve (the largest reservation per edge).
struct Reservation {
	UINT edge;
	QRect monitor;
	qint32 size;
};

std::vector<std::pair<HWND, Reservation>>& reservations() {
	static std::vector<std::pair<HWND, Reservation>> list;
	return list;
}

// Every Quickshell instance keeps the work area when Explorer is gone (Aether's shell and Ryoku's run side by side):
// each wrote only its own panels' reservations, the other answered with its own, and the work area flipped ~11 times
// a second - the window manager re-tiled and animated on every flip (2026-11-15). Reservations are shared here, so
// every instance computes the same work area.
constexpr auto SHARED_KEY = L"Software\\Quickshell\\WorkAreaReservations";

struct SharedReservation {
	DWORD pid;
	UINT edge;
	qint32 x, y, w, h;
	qint32 size;
};

void shareReservation(HWND hwnd, const Reservation& r) {
	SharedReservation data {
	    GetCurrentProcessId(),
	    r.edge,
	    r.monitor.left(),
	    r.monitor.top(),
	    r.monitor.width(),
	    r.monitor.height(),
	    r.size,
	};
	RegSetKeyValueW(HKEY_CURRENT_USER, SHARED_KEY, registeredName(hwnd).c_str(), REG_BINARY, &data, sizeof(data));
}

void unshareReservation(HWND hwnd) {
	RegDeleteKeyValueW(HKEY_CURRENT_USER, SHARED_KEY, registeredName(hwnd).c_str());
}

// all live reservations, this instance's and the others' (entries of closed windows are dropped)
std::vector<Reservation> allReservations() {
	std::vector<Reservation> out;
	for (const auto& [hwnd, r]: reservations()) out.push_back(r);

	HKEY key = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, SHARED_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
		return out;
	}
	std::vector<std::wstring> dead;
	for (DWORD i = 0;; i++) {
		wchar_t name[64] {};
		DWORD nameLength = std::size(name);
		DWORD type = 0;
		SharedReservation data {};
		DWORD size = sizeof(data);
		auto status = RegEnumValueW(key, i, name, &nameLength, nullptr, &type, reinterpret_cast<BYTE*>(&data), &size);
		if (status == ERROR_MORE_DATA) continue;
		if (status != ERROR_SUCCESS) break;
		if (type != REG_BINARY || size != sizeof(data)) continue;
		if (data.pid == GetCurrentProcessId()) continue; // ours are in reservations() already

		auto* hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(std::wcstoull(name, nullptr, 16)));
		DWORD owner = 0;
		if (hwnd == nullptr || !IsWindow(hwnd) || (GetWindowThreadProcessId(hwnd, &owner), owner != data.pid)) {
			dead.emplace_back(name, nameLength);
			continue;
		}
		out.push_back(Reservation {data.edge, QRect(data.x, data.y, data.w, data.h), data.size});
	}
	for (const auto& name: dead) RegDeleteValueW(key, name.c_str());
	RegCloseKey(key);
	return out;
}

// Work-area writes run on their own thread. SPI_SETWORKAREA with SPIF_SENDCHANGE broadcasts WM_SETTINGCHANGE and
// waits on every top-level window, so one hung app anywhere (Discord, an overlay, a crash handler...) froze the
// whole shell on its UI thread ("Not responding", 2026-11-08). The write itself can also block for seconds under
// load (same finding as CaelestiaWin's 06_layout.cpp). Here: the newest rect per monitor wins, and apps are told
// with SendNotifyMessage, which never waits on them.
class WorkAreaWriter {
public:
	void post(const QRect& monitor, const RECT& work) {
		std::lock_guard lock(this->mutex);
		this->pending[{monitor.left(), monitor.top(), monitor.width(), monitor.height()}] = work;
		if (!this->started) {
			this->started = true;
			std::thread([this]() { this->run(); }).detach();
		}
		this->wake.notify_one();
	}

private:
	using Key = std::tuple<qint32, qint32, qint32, qint32>;

	void run() {
		for (;;) {
			std::map<Key, RECT> batch;
			{
				std::unique_lock lock(this->mutex);
				this->wake.wait(lock, [this]() { return !this->pending.empty(); });
				batch.swap(this->pending);
			}

			for (auto& [key, work]: batch) {
				// session only (no SPIF_UPDATEINIFILE), like Explorer
				if (!SystemParametersInfoW(SPI_SETWORKAREA, 0, &work, 0)) continue;
				// what SPIF_SENDCHANGE would send (maximized windows re-fit), without waiting on hung apps
				SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
				qCInfo(logAppBar) << "Work area set to" << work.left << work.top << work.right << work.bottom
				                  << "(no Explorer to keep it)";
			}
		}
	}

	std::mutex mutex;
	std::condition_variable wake;
	std::map<Key, RECT> pending;
	bool started = false;
};

WorkAreaWriter& workAreaWriter() {
	// never destroyed: its detached thread may still be inside SystemParametersInfo at exit
	static auto* writer = new WorkAreaWriter();
	return *writer;
}

void applyWorkArea(const QRect& monitor) {
	qint32 left = 0;
	qint32 top = 0;
	qint32 right = 0;
	qint32 bottom = 0;
	for (const auto& r: allReservations()) {
		if (r.monitor != monitor) continue;
		switch (r.edge) {
		case ABE_LEFT: left = qMax(left, r.size); break;
		case ABE_TOP: top = qMax(top, r.size); break;
		case ABE_RIGHT: right = qMax(right, r.size); break;
		case ABE_BOTTOM: bottom = qMax(bottom, r.size); break;
		default: break;
		}
	}

	RECT work {
	    monitor.left() + left,
	    monitor.top() + top,
	    monitor.left() + monitor.width() - right,
	    monitor.top() + monitor.height() - bottom,
	};

	POINT centre {monitor.left() + monitor.width() / 2, monitor.top() + monitor.height() / 2};
	MONITORINFO info {};
	info.cbSize = sizeof(info);
	if (GetMonitorInfoW(MonitorFromPoint(centre, MONITOR_DEFAULTTONEAREST), &info)
	    && EqualRect(&info.rcWork, &work))
	{
		return;
	}

	workAreaWriter().post(monitor, work);
}

} // namespace

void WinAppBar::removeStale() {
	HKEY key = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTERED_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key)
	    != ERROR_SUCCESS)
	{
		return;
	}

	auto entries = std::vector<std::pair<std::wstring, DWORD>>();
	for (DWORD i = 0;; i++) {
		wchar_t name[64] {};
		DWORD nameLength = std::size(name);
		DWORD type = 0;
		DWORD pid = 0;
		DWORD size = sizeof(pid);

		auto status = RegEnumValueW(key, i, name, &nameLength, nullptr, &type, reinterpret_cast<BYTE*>(&pid), &size);
		if (status == ERROR_MORE_DATA) continue;
		if (status != ERROR_SUCCESS) break;
		entries.emplace_back(std::wstring(name, nameLength), type == REG_DWORD ? pid : 0);
	}

	auto removed = 0;
	for (const auto& [name, pid]: entries) {
		auto* hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(std::wcstoull(name.c_str(), nullptr, 16)));

		if (hwnd != nullptr && IsWindow(hwnd)) {
			DWORD owner = 0;
			GetWindowThreadProcessId(hwnd, &owner);
			if (owner == pid) continue;
		} else if (hwnd != nullptr) {
			auto yield = TrayHookYield();
			auto data = appBarData(hwnd);
			SHAppBarMessage(ABM_REMOVE, &data);
			removed++;
		}

		RegDeleteValueW(key, name.c_str());
	}

	RegCloseKey(key);

	if (removed != 0) {
		qCInfo(logAppBar) << "Removed" << removed << "AppBar registrations left by a closed instance";
	}
}

UINT WinAppBar::callbackMessage() {
	static const UINT message = RegisterWindowMessageW(L"QuickshellAppBarNotify"); // NOLINT
	return message;
}

UINT WinAppBar::taskbarCreatedMessage() {
	static const UINT message = RegisterWindowMessageW(L"TaskbarCreated"); // NOLINT
	return message;
}

bool WinAppBar::ensureRegistered(HWND hwnd) {
	auto yield = TrayHookYield();
	if (this->mRegistered && this->mHwnd == hwnd) return true;
	if (this->mRegistered) this->remove();

	static auto staleRemoved = false;
	if (!staleRemoved) {
		staleRemoved = true;
		WinAppBar::removeStale();
	}

	auto data = appBarData(hwnd);
	if (!SHAppBarMessage(ABM_NEW, &data)) {
		SHAppBarMessage(ABM_REMOVE, &data);
		data = appBarData(hwnd);
		if (!SHAppBarMessage(ABM_NEW, &data)) {
			qCWarning(logAppBar) << "Failed to register AppBar for window" << hwnd;
			return false;
		}
		qCDebug(logAppBar) << "Replaced a stale AppBar registration for" << hwnd;
	}

	rememberRegistration(hwnd);
	this->mHwnd = hwnd;
	this->mRegistered = true;
	this->mReserved = QRect();
	this->mPositionValid = false;
	return true;
}

QRect WinAppBar::reserve(HWND hwnd, UINT edge, const QRect& monitor, qint32 size) {
	if (hwnd == nullptr || size <= 0 || !monitor.isValid()) return {};

	if (this->mPositionValid && this->mRegistered && this->mHwnd == hwnd && this->mEdge == edge
	    && this->mMonitor == monitor && this->mSize == size)
	{
		return this->mReserved;
	}

	auto yield = TrayHookYield();
	if (!this->ensureRegistered(hwnd)) {
		// no AppBar server: keep the work area ourselves
		auto& list = reservations();
		std::erase_if(list, [hwnd](const auto& entry) { return entry.first == hwnd; });
		list.emplace_back(hwnd, Reservation {edge, monitor, size});
		shareReservation(hwnd, list.back().second);
		if (this->mFallbackMonitor.isValid() && this->mFallbackMonitor != monitor) applyWorkArea(this->mFallbackMonitor);
		applyWorkArea(monitor);
		this->mFallback = hwnd;
		this->mFallbackMonitor = monitor;

		auto granted = monitor;
		switch (edge) {
		case ABE_TOP: granted.setHeight(size); break;
		case ABE_BOTTOM: granted.setTop(monitor.bottom() - size + 1); break;
		case ABE_LEFT: granted.setWidth(size); break;
		case ABE_RIGHT: granted.setLeft(monitor.right() - size + 1); break;
		default: break;
		}
		return granted;
	}

	auto data = appBarData(hwnd);
	data.uEdge = edge;
	data.rc = toRECT(monitor);

	auto applySize = [&]() {
		switch (edge) {
		case ABE_TOP: data.rc.bottom = data.rc.top + size; break;
		case ABE_BOTTOM: data.rc.top = data.rc.bottom - size; break;
		case ABE_LEFT: data.rc.right = data.rc.left + size; break;
		case ABE_RIGHT: data.rc.left = data.rc.right - size; break;
		default: break;
		}
	};

	applySize();
	SHAppBarMessage(ABM_QUERYPOS, &data);
	applySize();

	auto granted = toQRect(data.rc);

	if (this->mEdge != edge || this->mReserved != granted) {
		SHAppBarMessage(ABM_SETPOS, &data);
		this->mEdge = edge;
		this->mReserved = toQRect(data.rc);
		qCDebug(logAppBar) << "Reserved" << this->mReserved << "on edge" << edge << "for" << hwnd;
	}

	this->mMonitor = monitor;
	this->mSize = size;
	this->mPositionValid = true;
	return this->mReserved;
}

void WinAppBar::invalidatePosition() { this->mPositionValid = false; }

void WinAppBar::remove() {
	if (this->mFallback != nullptr) {
		auto* hwnd = this->mFallback;
		std::erase_if(reservations(), [hwnd](const auto& entry) { return entry.first == hwnd; });
		unshareReservation(hwnd);
		this->mFallback = nullptr;
		if (this->mFallbackMonitor.isValid()) applyWorkArea(this->mFallbackMonitor);
		this->mFallbackMonitor = QRect();
	}
	if (!this->mRegistered) return;
	auto yield = TrayHookYield();

	auto data = appBarData(this->mHwnd);
	SHAppBarMessage(ABM_REMOVE, &data);
	forgetRegistration(this->mHwnd);
	this->invalidate();
}

void WinAppBar::invalidate() {
	this->mRegistered = false;
	this->mHwnd = nullptr;
	this->mEdge = 0;
	this->mReserved = QRect();
	this->mMonitor = QRect();
	this->mSize = 0;
	this->mPositionValid = false;
}

void WinAppBar::adopt(WinAppBar& other) {
	if (&other == this) return;
	this->remove();

	this->mHwnd = other.mHwnd;
	this->mRegistered = other.mRegistered;
	this->mEdge = other.mEdge;
	this->mReserved = other.mReserved;
	this->mMonitor = other.mMonitor;
	this->mSize = other.mSize;
	this->mPositionValid = other.mPositionValid;
	other.invalidate();
}

void WinAppBar::notifyActivate() {
	if (!this->mRegistered) return;
	auto yield = TrayHookYield();
	auto data = appBarData(this->mHwnd);
	SHAppBarMessage(ABM_ACTIVATE, &data);
}

void WinAppBar::notifyWindowPosChanged() {
	if (!this->mRegistered) return;
	auto yield = TrayHookYield();
	auto data = appBarData(this->mHwnd);
	SHAppBarMessage(ABM_WINDOWPOSCHANGED, &data);
}

} // namespace qs::windows
