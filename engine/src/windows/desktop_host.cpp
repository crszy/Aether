#include "desktop_host.hpp"
#include <cwchar>

#include <qt_windows.h>

#include <qcoreapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpointer.h>
#include <qstringlist.h>
#include <qtimer.h>

#include "../core/logcat.hpp"
#include "appbar.hpp"
#include "util.hpp"

namespace qs::windows {

namespace {

QS_LOGGING_CATEGORY(logDesktop, "quickshell.windows.desktop", QtWarningMsg);

constexpr const wchar_t* LISTENER_CLASS = L"QuickshellDesktopListener";

constexpr UINT SPAWN_WORKERW = 0x052C;
constexpr UINT SPAWN_TIMEOUT_MS = 150;
constexpr int SPAWN_RETRY_MS = 500;

constexpr int SETTLE_MS = 500;

constexpr DWORD BUILD_24H2 = 26100;

DesktopHost* gHost = nullptr; // NOLINT

DWORD processOf(HWND hwnd) {
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	return pid;
}

bool hasClass(HWND hwnd, const wchar_t* name) {
	wchar_t cls[32] {};
	if (GetClassNameW(hwnd, cls, 32) == 0) return false;
	return wcscmp(cls, name) == 0;
}

struct Lookup {
	HWND parent = nullptr;
	HWND insertAfter = nullptr;
	const char* layout = "";
};

HWND findTopLevelIconsHost(HWND progman) {
	struct Search {
		DWORD pid = 0;
		HWND iconsHost = nullptr;
	} search {.pid = processOf(progman)};

	EnumWindows(
	    [](HWND hwnd, LPARAM param) -> BOOL {
		    auto* search = reinterpret_cast<Search*>(param); // NOLINT(performance-no-int-to-ptr)
		    if (FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr) == nullptr) return TRUE;
		    if (processOf(hwnd) != search->pid) return TRUE;
		    search->iconsHost = hwnd;
		    return FALSE;
	    },
	    reinterpret_cast<LPARAM>(&search)
	);

	return search.iconsHost;
}

HWND findIconsView(HWND progman) {
	auto* icons = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);

	if (icons == nullptr) {
		auto* host = findTopLevelIconsHost(progman);
		if (host != nullptr) icons = FindWindowExW(host, nullptr, L"SHELLDLL_DefView", nullptr);
	}

	return icons != nullptr && IsWindowVisible(icons) ? icons : nullptr;
}

Lookup findChildWorkerW(HWND progman) {
	auto* workerw = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
	if (workerw == nullptr || !IsWindowVisible(workerw)) return {};
	return {.parent = workerw, .layout = "WorkerW inside Progman"};
}

Lookup findTopLevelWorkerW(HWND progman) {
	auto* iconsHost = findTopLevelIconsHost(progman);
	if (iconsHost == nullptr) return {};

	auto pid = processOf(progman);
	auto* workerw = FindWindowExW(nullptr, iconsHost, L"WorkerW", nullptr);
	if (workerw == nullptr || processOf(workerw) != pid || !IsWindowVisible(workerw)) {
		return {};
	}

	if (FindWindowExW(workerw, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr) return {};
	return {.parent = workerw, .layout = "WorkerW behind the icons"};
}

Lookup findProgman(HWND progman) {
	auto* icons = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
	if (icons == nullptr) return {};
	return {.parent = progman, .insertAfter = icons, .layout = "Progman, below the icons"};
}

Lookup findWorkerW(HWND progman) {
	auto childLayout = windowsBuild() >= BUILD_24H2;
	auto found = childLayout ? findChildWorkerW(progman) : findTopLevelWorkerW(progman);
	if (found.parent == nullptr) {
		found = childLayout ? findTopLevelWorkerW(progman) : findChildWorkerW(progman);
	}

	return found;
}

} // namespace

DesktopHost* DesktopHost::instance() {
	static QPointer<DesktopHost> host; // NOLINT

	if (host.isNull()) {
		host = new DesktopHost(QCoreApplication::instance());
	}

	return host.data();
}

DesktopHost::DesktopHost(QObject* parent): QObject(parent) {
	gHost = this;
	this->refreshTimer.setSingleShot(true);
	QObject::connect(&this->refreshTimer, &QTimer::timeout, this, &DesktopHost::refresh);
}

DesktopHost::~DesktopHost() {
	this->removeHook();
	if (this->listener != nullptr) DestroyWindow(this->listener);
	delete this->desktopKeyNotifier;
	if (this->desktopKeyEvent != nullptr) CloseHandle(this->desktopKeyEvent);
	if (this->desktopKey != nullptr) RegCloseKey(this->desktopKey);
	if (gHost == this) gHost = nullptr;
}

void DesktopHost::setEnabled(bool enabled) {
	if (enabled == this->mEnabled) return;
	this->mEnabled = enabled;
	emit this->enabledChanged();
	this->refresh();
}

HWND DesktopHost::parentWindow() {
	if (!this->mEnabled) return nullptr;

	if (!this->lookedUp || (this->mParent != nullptr && !IsWindow(this->mParent))) {
		this->refresh();
	}

	return this->mParent;
}

HWND DesktopHost::iconsHost() {
	if (!this->mEnabled) return nullptr;

	if (!this->lookedUp || (this->mIconsHost != nullptr && !IsWindow(this->mIconsHost))) {
		this->refresh();
	}

	return this->mIconsHost;
}

void DesktopHost::setAboveIcons(const QStringList& namespaces) {
	if (namespaces == this->mAboveIcons) return;
	this->mAboveIcons = namespaces;
	emit this->aboveIconsChanged();
}

void DesktopHost::scheduleRefresh(int delayMs) { this->refreshTimer.start(delayMs); }

void DesktopHost::refresh() {
	this->refreshTimer.stop();

	auto* oldParent = this->mParent;
	auto* oldInsertAfter = this->mInsertAfter;
	auto* oldIconsHost = this->mIconsHost;
	auto wasActive = this->active();

	if (this->mEnabled) {
		this->ensureListener();
		this->lookup();
		this->installHook();
	} else {
		this->mParent = nullptr;
		this->mInsertAfter = nullptr;
		this->mIconsView = nullptr;
		this->mIconsHost = nullptr;
		this->lookedUp = false;
		this->removeHook();
	}

	if (this->mParent != oldParent || this->mInsertAfter != oldInsertAfter
	    || this->mIconsHost != oldIconsHost)
	{
		emit this->parentChanged();
	} else if (this->mParent != nullptr) {
		emit this->parentMoved();
	}

	if (this->active() != wasActive) emit this->activeChanged();
}

void DesktopHost::lookup() {
	auto first = !this->lookedUp;
	this->lookedUp = true;

	Lookup found;
	auto* progman = FindWindowW(L"Progman", nullptr);
	auto neededSpawn = false;

	if (progman != nullptr) {
		found = findWorkerW(progman);

		if (found.parent == nullptr) {
			neededSpawn = true;
			DWORD_PTR result = 0;
			auto flags = SMTO_NORMAL | SMTO_ABORTIFHUNG;
			SendMessageTimeoutW(progman, SPAWN_WORKERW, 0xD, 0x1, flags, SPAWN_TIMEOUT_MS, &result);
			found = findWorkerW(progman);

			if (found.parent == nullptr) {
				SendMessageTimeoutW(progman, SPAWN_WORKERW, 0, 0, flags, SPAWN_TIMEOUT_MS, &result);
				found = findWorkerW(progman);
			}
		}

		auto gotWorkerW = found.parent != nullptr;
		if (!gotWorkerW) found = findProgman(progman);

		if (gotWorkerW) {
			this->spawnRetriesLeft = 4;
		} else if (neededSpawn && this->spawnRetriesLeft > 0) {
			this->spawnRetriesLeft--;
			this->scheduleRefresh(SPAWN_RETRY_MS);
		}
	}

	auto* iconsView = progman == nullptr ? nullptr : findIconsView(progman);

	if ((first || iconsView != this->mIconsView) && iconsView == nullptr && found.parent != nullptr) {
		qCWarning(logDesktop) << "No desktop icons view found; desktop panels that take input"
		                      << "stay top level windows";
	}

	if (first || found.parent != this->mParent) {
		if (found.parent != nullptr) {
			qCInfo(logDesktop) << "Desktop panels go into" << found.layout << "on build"
			                   << windowsBuild();
		} else {
			qCWarning(logDesktop) << "No desktop window found (explorer not running?);"
			                      << "desktop panels stay top level windows";
		}
	}

	this->mParent = found.parent;
	this->mInsertAfter = found.insertAfter;
	this->mIconsView = iconsView;
	this->mIconsHost = iconsView == nullptr ? nullptr : GetAncestor(iconsView, GA_PARENT);
}

void DesktopHost::watchWallpaperKey() {
	if (this->desktopKey == nullptr) {
		if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_NOTIFY, &this->desktopKey) != ERROR_SUCCESS) {
			this->desktopKey = nullptr;
			return;
		}
		this->desktopKeyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		this->desktopKeyNotifier = new QWinEventNotifier(this->desktopKeyEvent, this);
		QObject::connect(this->desktopKeyNotifier, &QWinEventNotifier::activated, this, [this]() {
			this->watchWallpaperKey();
			emit this->wallpaperChanged();
		});
	}

	RegNotifyChangeKeyValue(this->desktopKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, this->desktopKeyEvent, TRUE);
}

void DesktopHost::ensureListener() {
	this->watchWallpaperKey();
	if (this->listener != nullptr) return;

	auto* module = GetModuleHandleW(nullptr);
	WNDCLASSW cls {};
	cls.lpfnWndProc = &DesktopHost::listenerProc;
	cls.hInstance = module;
	cls.lpszClassName = LISTENER_CLASS;
	RegisterClassW(&cls);

	this->listener = CreateWindowExW(
	    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
	    LISTENER_CLASS,
	    L"",
	    WS_POPUP,
	    0,
	    0,
	    0,
	    0,
	    nullptr,
	    nullptr,
	    module,
	    nullptr
	);

	if (this->listener == nullptr) {
		qCWarning(logDesktop) << "Failed to create the desktop listener window:" << GetLastError()
		                      << "- explorer restarts won't be followed";
		return;
	}

	ChangeWindowMessageFilterEx(
	    this->listener,
	    WinAppBar::taskbarCreatedMessage(),
	    MSGFLT_ALLOW,
	    nullptr
	);
}

LRESULT CALLBACK DesktopHost::listenerProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	if (gHost != nullptr) {
		if (msg == WinAppBar::taskbarCreatedMessage()) {
			gHost->scheduleRefresh();
		} else if (msg == WM_DISPLAYCHANGE) {
			gHost->scheduleRefresh(SETTLE_MS);
		} else if (msg == WM_SETTINGCHANGE && wparam == SPI_SETDESKWALLPAPER) {
			gHost->scheduleRefresh(SETTLE_MS);
		}
	}

	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void DesktopHost::installHook() {
	auto* target = this->mParent != nullptr ? this->mParent : FindWindowW(L"Progman", nullptr);
	DWORD pid = 0;
	auto thread = target == nullptr ? 0 : GetWindowThreadProcessId(target, &pid);
	if (thread == this->hookThread && (thread == 0 || this->hook != nullptr)) return;

	this->removeHook();
	if (thread == 0) return;

	this->hook = SetWinEventHook(
	    EVENT_OBJECT_CREATE,
	    EVENT_OBJECT_REORDER,
	    nullptr,
	    &DesktopHost::eventProc,
	    pid,
	    thread,
	    WINEVENT_OUTOFCONTEXT
	);

	this->moveHook = SetWinEventHook(
	    EVENT_OBJECT_LOCATIONCHANGE,
	    EVENT_OBJECT_PARENTCHANGE,
	    nullptr,
	    &DesktopHost::eventProc,
	    pid,
	    thread,
	    WINEVENT_OUTOFCONTEXT
	);

	this->hookThread = thread;

	if (this->hook == nullptr || this->moveHook == nullptr) {
		qCWarning(logDesktop) << "Could not watch the desktop window:" << GetLastError();
	}
}

void DesktopHost::removeHook() {
	if (this->hook != nullptr) UnhookWinEvent(this->hook);
	if (this->moveHook != nullptr) UnhookWinEvent(this->moveHook);
	this->hook = nullptr;
	this->moveHook = nullptr;
	this->hookThread = 0;
}

void CALLBACK DesktopHost::eventProc(
    HWINEVENTHOOK /*hook*/,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD /*thread*/,
    DWORD /*time*/
) {
	auto* host = gHost;
	if (host == nullptr || hwnd == nullptr || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) {
		return;
	}

	auto scheduleMoved = [host] {
		if (host->movePending) return;
		host->movePending = true;

		QTimer::singleShot(0, host, [host] {
			host->movePending = false;
			emit host->parentMoved();
		});
	};

	if (hwnd == host->mParent) {
		switch (event) {
		case EVENT_OBJECT_LOCATIONCHANGE: scheduleMoved(); break;
		case EVENT_OBJECT_DESTROY:
		case EVENT_OBJECT_HIDE: host->scheduleRefresh(SETTLE_MS); break;
		default: break;
		}

		if (hwnd != host->mIconsHost) return;
	}

	if (event == EVENT_OBJECT_SHOW && (host->mParent == nullptr || host->mInsertAfter != nullptr)
	    && hasClass(hwnd, L"WorkerW"))
	{
		host->scheduleRefresh(SETTLE_MS);
		return;
	}

	if (host->mIconsView == nullptr) {
		if (event == EVENT_OBJECT_SHOW && host->mParent != nullptr
		    && hasClass(hwnd, L"SHELLDLL_DefView"))
		{
			host->scheduleRefresh(SETTLE_MS);
		}

		return;
	}

	if (hwnd == host->mIconsView) {
		if (event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_HIDE
		    || event == EVENT_OBJECT_PARENTCHANGE)
		{
			host->scheduleRefresh(SETTLE_MS);
		}

		return;
	}

	if (hwnd == host->mIconsHost) {
		switch (event) {
		case EVENT_OBJECT_DESTROY:
		case EVENT_OBJECT_HIDE: host->scheduleRefresh(SETTLE_MS); return;
		case EVENT_OBJECT_LOCATIONCHANGE: scheduleMoved(); return;
		case EVENT_OBJECT_REORDER: break;
		default: return;
		}
	} else if ((event != EVENT_OBJECT_CREATE && event != EVENT_OBJECT_SHOW)
	           || GetAncestor(hwnd, GA_PARENT) != host->mIconsHost)
	{
		return;
	}

	if (GetAncestor(host->mIconsView, GA_PARENT) != host->mIconsHost) {
		host->scheduleRefresh(SETTLE_MS);
		return;
	}

	if (!host->restackPending) {
		host->restackPending = true;

		QTimer::singleShot(0, host, [host] {
			host->restackPending = false;
			emit host->iconsRestacked();
		});
	}
}

DesktopLayer::DesktopLayer(QObject* parent): QObject(parent) {
	auto* host = DesktopHost::instance();
	QObject::connect(host, &DesktopHost::enabledChanged, this, &DesktopLayer::enabledChanged);
	QObject::connect(host, &DesktopHost::activeChanged, this, &DesktopLayer::activeChanged);
	QObject::connect(host, &DesktopHost::aboveIconsChanged, this, &DesktopLayer::aboveIconsChanged);
	QObject::connect(host, &DesktopHost::wallpaperChanged, this, &DesktopLayer::wallpaperChanged);
}

bool DesktopLayer::enabled() const { return DesktopHost::instance()->enabled(); }
void DesktopLayer::setEnabled(bool enabled) { DesktopHost::instance()->setEnabled(enabled); }
bool DesktopLayer::active() const { return DesktopHost::instance()->active(); }

QStringList DesktopLayer::aboveIcons() const { return DesktopHost::instance()->aboveIcons(); }

void DesktopLayer::setAboveIcons(const QStringList& namespaces) {
	DesktopHost::instance()->setAboveIcons(namespaces);
}

} // namespace qs::windows
