#include "virtual_desktops.hpp"
#include <algorithm>
#include <utility>

#include <qbytearray.h>
#include <qdir.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <qfileinfo.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qlocalsocket.h>
#include <qcoreapplication.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qstring.h>
#include <qthread.h>
#include <qtimer.h>
#include <qtypes.h>

#include "keyboard_hook.hpp"
#include "startup.hpp"
#include "util.hpp"

#include <objbase.h>
#include <shobjidl_core.h>
#include <tlhelp32.h>

namespace qs::windows {

namespace {
Q_LOGGING_CATEGORY(logDesktops, "quickshell.windows.desktops", QtWarningMsg);

constexpr ULONGLONG ACCESSOR_RESTART_INTERVAL = 60000;

template <typename R, typename... P, typename... A>
bool sehCall(R (*fn)(P...), R& out, A... args) {
	__try {
		out = fn(args...);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool sehCallVoid(void (*fn)()) {
	__try {
		fn();
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

constexpr const wchar_t* DESKTOPS_KEY =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";
constexpr const wchar_t* SESSION_KEY_FORMAT =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SessionInfo\\%1\\VirtualDesktops";

constexpr UINT ACCESSOR_MESSAGE = WM_APP + 0x100;
constexpr const wchar_t* LISTENER_CLASS = L"QuickshellVirtualDesktops";

constexpr qsizetype MAX_DESKTOPS = 20;
constexpr int CREATION_POLL_MS = 25;
constexpr int CREATION_POLLS = 40;
constexpr int CLOAK_REFRESH_DELAY_MS = 250;

// window properties of Caelestia's own workspaces; they live on the window, so they outlive a crash of the shell
constexpr const wchar_t* OWN_HIDDEN_PROP = L"CaelestiaWorkspaceHidden";
constexpr const wchar_t* OWN_WORKSPACE_PROP = L"CaelestiaWorkspace";   // index + 1
constexpr const wchar_t* OWN_PINNED_PROP = L"CaelestiaWorkspacePinned";
constexpr const wchar_t* OWN_SPECIAL_PROP = L"CaelestiaWorkspaceSpecial";   // a global atom holding the name

// Without Explorer, Windows shows minimized windows as small title bars ("bubbles") at the bottom left of the
// screen. Explorer turns that off when it starts (ARW_HIDE: minimized windows go to -32000); the shell does the
// same, for this session only, and moves bubbles already on screen out of sight - now and every few seconds
// (a window minimized before this ran keeps its bubble spot until moved).
void parkBubbles() {
	EnumWindows(
	    [](HWND hwnd, LPARAM) -> BOOL {
		    if (!IsIconic(hwnd) || !IsWindowVisible(hwnd)) return TRUE;
		    RECT r {};
		    if (!GetWindowRect(hwnd, &r) || r.left <= -30000) return TRUE;
		    SetWindowPos(hwnd, nullptr, -32000, -32000, 0, 0,
		                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
		    return TRUE;
	    },
	    0
	);
}

void hideMinimizedBubbles(QObject* owner) {
	MINIMIZEDMETRICS metrics {};
	metrics.cbSize = sizeof(metrics);
	if (SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS, sizeof(metrics), &metrics, 0)
	    && (metrics.iArrange & ARW_HIDE) == 0)
	{
		metrics.iArrange |= ARW_HIDE;
		systemParametersNoWait(SPI_SETMINIMIZEDMETRICS, sizeof(metrics), &metrics, SPIF_SENDCHANGE);
	}
	parkBubbles();
	auto* timer = new QTimer(owner);
	timer->setInterval(3000);
	QObject::connect(timer, &QTimer::timeout, owner, []() { parkBubbles(); });
	timer->start();
}

bool explorerRunning() {
	// the taskbar of explorer.exe (Caelestia's bar does not use this class) means Windows' desktop service is up
	auto* tray = FindWindowW(L"Shell_TrayWnd", nullptr);
	if (tray == nullptr) return false;
	DWORD pid = 0;
	GetWindowThreadProcessId(tray, &pid);
	return pid != GetCurrentProcessId();
}

bool readBinary(HKEY key, const wchar_t* name, QByteArray& out) {
	DWORD type = 0;
	DWORD size = 0;
	if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) return false;
	if (type != REG_BINARY || size == 0) return false;

	out.resize(static_cast<qsizetype>(size));
	auto* data = reinterpret_cast<BYTE*>(out.data());
	return RegQueryValueExW(key, name, nullptr, nullptr, data, &size) == ERROR_SUCCESS;
}

QString readString(HKEY key, const wchar_t* name) {
	DWORD type = 0;
	DWORD size = 0;
	if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) return {};
	if (type != REG_SZ || size < sizeof(wchar_t)) return {};

	auto buffer = QByteArray(static_cast<qsizetype>(size), '\0');
	auto* data = reinterpret_cast<BYTE*>(buffer.data());
	if (RegQueryValueExW(key, name, nullptr, nullptr, data, &size) != ERROR_SUCCESS) return {};

	auto* chars = reinterpret_cast<const wchar_t*>(buffer.constData());
	return QString::fromWCharArray(chars, static_cast<qsizetype>(size / sizeof(wchar_t))).trimmed()
	    .remove(QChar('\0'));
}

GUID guidAt(const QByteArray& bytes, qsizetype offset) {
	GUID guid {};
	memcpy(&guid, bytes.constData() + offset, sizeof(GUID));
	return guid;
}

bool guidIsNull(const GUID& guid) {
	static const GUID null {};
	return IsEqualGUID(guid, null);
}

bool isOwnWindow(HWND hwnd) {
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	return pid == GetCurrentProcessId();
}

} // namespace

RegistryWatcher::RegistryWatcher(QList<QString> subkeys, QObject* parent)
    : QThread(parent)
    , subkeys(std::move(subkeys)) {
	this->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

RegistryWatcher::~RegistryWatcher() {
	this->stop();
	if (this->stopEvent != nullptr) CloseHandle(this->stopEvent);
}

void RegistryWatcher::stop() {
	if (!this->isRunning()) return;
	SetEvent(this->stopEvent);
	this->wait();
}

void RegistryWatcher::run() {
	QList<HKEY> keys;
	QList<HANDLE> events;
	QList<bool> armed;

	for (const auto& subkey: this->subkeys) {
		HKEY key = nullptr;
		auto path = subkey.toStdWString();
		if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_NOTIFY, &key) != ERROR_SUCCESS) {
			qCDebug(logDesktops) << "Not watching missing registry key" << subkey;
			continue;
		}

		keys.append(key);
		events.append(CreateEventW(nullptr, FALSE, FALSE, nullptr));
		armed.append(false);
	}

	if (keys.isEmpty()) {
		qCWarning(logDesktops) << "No virtual desktop registry keys to watch.";
		return;
	}

	QList<HANDLE> handles;
	handles.append(this->stopEvent);
	handles.append(events);

	while (true) {
		for (qsizetype i = 0; i < keys.length(); i++) {
			if (armed[i]) continue;
			auto status = RegNotifyChangeKeyValue(
			    keys[i],
			    TRUE,
			    REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
			    events[i],
			    TRUE
			);
			armed[i] = status == ERROR_SUCCESS;
		}

		auto result = WaitForMultipleObjects(
		    static_cast<DWORD>(handles.length()),
		    handles.constData(),
		    FALSE,
		    INFINITE
		);

		if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) break;

		auto index = static_cast<qsizetype>(result - WAIT_OBJECT_0 - 1);
		if (index >= 0 && index < armed.length()) armed[index] = false;
		emit this->changed();
	}

	for (auto* key: keys) RegCloseKey(key);
	for (auto* event: events) CloseHandle(event);
}

VirtualDesktops* VirtualDesktops::instance() {
	static auto* instance = new VirtualDesktops(); // NOLINT
	return instance;
}

VirtualDesktops::VirtualDesktops() {
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	ProcessIdToSessionId(GetCurrentProcessId(), &this->sessionId);
	this->refresh(true);

	this->cloakTimer.setSingleShot(true);
	this->cloakTimer.setInterval(CLOAK_REFRESH_DELAY_MS);
	QObject::connect(&this->cloakTimer, &QTimer::timeout, this, [this]() { this->refresh(); });

	this->creationTimer.setInterval(CREATION_POLL_MS);
	QObject::connect(&this->creationTimer, &QTimer::timeout, this, &VirtualDesktops::pollCreation);

	// windows an earlier run hid on another workspace (it crashed, or was restarted)
	EnumWindows(
	    [](HWND hwnd, LPARAM param) -> BOOL {
		    if (GetPropW(hwnd, OWN_HIDDEN_PROP) != nullptr) {
			    reinterpret_cast<QList<HWND>*>(param)->append(hwnd); // NOLINT(performance-no-int-to-ptr)
		    }
		    return TRUE;
	    },
	    reinterpret_cast<LPARAM>(&this->leftHidden)
	);

	startup::afterFirstFrame(this, [this]() {
		this->loadAccessor();
		this->installListener();
		this->refresh();
		if (this->accessor.loaded) emit this->accessorReady();

		if (!explorerRunning()) hideMinimizedBubbles(this);
		if (!this->accessor.loaded && !explorerRunning()) {
			this->enableOwn();
		} else {
			// Windows' own desktops are back: give every window we hid back
			for (auto* hwnd: std::exchange(this->leftHidden, {})) {
				RemovePropW(hwnd, OWN_HIDDEN_PROP);
				if (IsWindow(hwnd)) ShowWindowAsync(hwnd, SW_SHOWNA);
			}
		}
	});

	auto sessionKey = QString::fromWCharArray(SESSION_KEY_FORMAT).arg(this->sessionId);
	this->watcher = new RegistryWatcher({QString::fromWCharArray(DESKTOPS_KEY), sessionKey}, this);
	QObject::connect(this->watcher, &RegistryWatcher::changed, this, [this]() {
		this->refresh(true);
	});
	this->watcher->start();

	if (auto* app = QCoreApplication::instance()) {
		QObject::connect(app, &QCoreApplication::aboutToQuit, this->watcher, &RegistryWatcher::stop);
	}
}

VirtualDesktops::~VirtualDesktops() {
	if (this->watcher != nullptr) this->watcher->stop();

	if (this->listener != nullptr) {
		if (this->accessor.unregisterPostMessageHook != nullptr) {
			this->call(-1, this->accessor.unregisterPostMessageHook, this->listener);
		}

		DestroyWindow(this->listener);
	}

	if (this->mManager != nullptr) this->mManager->Release();
	if (this->accessor.module != nullptr) FreeLibrary(this->accessor.module);
}

template <typename R, typename... P, typename... A>
R VirtualDesktops::call(R fallback, R (*fn)(P...), A... args) const {
	if (fn == nullptr) return fallback;

	R result = fallback;
	if (sehCall(fn, result, args...)) return result;

	this->accessorFailed();
	return fallback;
}

void VirtualDesktops::accessorFailed() const {
	auto& a = this->accessor;
	if (!a.loaded) return;

	auto now = GetTickCount64();
	if (a.restart != nullptr
	    && (this->accessorRestartedAt == 0 || now - this->accessorRestartedAt > ACCESSOR_RESTART_INTERVAL))
	{
		this->accessorRestartedAt = now;
		qCWarning(logDesktops) << "VirtualDesktopAccessor.dll crashed; restarting it";
		if (sehCallVoid(a.restart)) return;
	}

	qCWarning(logDesktops) << "VirtualDesktopAccessor.dll keeps crashing; using the registry and"
	                       << "keyboard shortcuts instead";
	a.loaded = false;
}

void VirtualDesktops::loadAccessor() {
	auto path = QCoreApplication::applicationDirPath()
	          + (windowsBuild() < 22000 ? "/win10/VirtualDesktopAccessor.dll" : "/VirtualDesktopAccessor.dll");
	auto wpath = path.toStdWString();
	auto* module = LoadLibraryW(wpath.c_str());

	if (module == nullptr) {
		qCInfo(logDesktops) << "VirtualDesktopAccessor.dll not found next to the executable;"
		                    << "desktop switching falls back to keyboard shortcuts.";
		return;
	}

	auto& a = this->accessor;
	a.module = module;

	auto load = [module](auto& fn, const char* name) {
		fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(module, name));
		return fn != nullptr;
	};

	auto ok = load(a.getCurrentDesktopNumber, "GetCurrentDesktopNumber")
	       && load(a.getDesktopCount, "GetDesktopCount")
	       && load(a.goToDesktopNumber, "GoToDesktopNumber")
	       && load(a.moveWindowToDesktopNumber, "MoveWindowToDesktopNumber")
	       && load(a.isWindowOnCurrentVirtualDesktop, "IsWindowOnCurrentVirtualDesktop");

	load(a.getDesktopIdByNumber, "GetDesktopIdByNumber");
	load(a.getWindowDesktopNumber, "GetWindowDesktopNumber");
	load(a.createDesktop, "CreateDesktop");
	load(a.removeDesktop, "RemoveDesktop");
	load(a.registerPostMessageHook, "RegisterPostMessageHook");
	load(a.unregisterPostMessageHook, "UnregisterPostMessageHook");
	load(a.isPinnedWindow, "IsPinnedWindow");
	load(a.pinWindow, "PinWindow");
	load(a.unPinWindow, "UnPinWindow");
	load(a.restart, "RestartVirtualDesktopAccessor");

	if (!ok || this->call(-1, a.getDesktopCount) <= 0) {
		qCWarning(logDesktops) << "VirtualDesktopAccessor.dll" << path
		                       << "does not work on this Windows build; ignoring it.";
		FreeLibrary(module);
		a = Accessor();
		return;
	}

	a.loaded = true;
	qCInfo(logDesktops) << "Loaded" << path;
}

LRESULT CALLBACK VirtualDesktops::listenerProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	if (msg == ACCESSOR_MESSAGE) {
		VirtualDesktops::instance()->refresh();
		return 0;
	}

	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void VirtualDesktops::installListener() {
	if (this->accessor.registerPostMessageHook == nullptr) return;

	auto* module = GetModuleHandleW(nullptr);
	WNDCLASSW cls {};
	cls.lpfnWndProc = &VirtualDesktops::listenerProc;
	cls.hInstance = module;
	cls.lpszClassName = LISTENER_CLASS;
	RegisterClassW(&cls);

	this->listener = CreateWindowExW(
	    0,
	    LISTENER_CLASS,
	    L"",
	    0,
	    0,
	    0,
	    0,
	    0,
	    HWND_MESSAGE,
	    nullptr,
	    module,
	    nullptr
	);

	if (this->listener == nullptr) {
		qCWarning(logDesktops) << "Failed to create the desktop change listener window:"
		                       << GetLastError();
		return;
	}

	this->call(-1, this->accessor.registerPostMessageHook, this->listener, ACCESSOR_MESSAGE);
}

QString VirtualDesktops::readDesktopName(const GUID& id) const {
	if (guidIsNull(id)) return {};

	HKEY key = nullptr;
	auto sub = QString::fromWCharArray(DESKTOPS_KEY) + "\\Desktops\\" + guidToString(id);
	auto wsub = sub.toStdWString();
	if (RegOpenKeyExW(HKEY_CURRENT_USER, wsub.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
		return {};
	}

	auto name = readString(key, L"Name");
	RegCloseKey(key);
	return name;
}

bool VirtualDesktops::readRegistry(QList<GUID>& ids, GUID& current) const {
	HKEY key = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, DESKTOPS_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) {
		return false;
	}

	QByteArray blob;
	if (readBinary(key, L"VirtualDesktopIDs", blob)) {
		for (qsizetype offset = 0; offset + static_cast<qsizetype>(sizeof(GUID)) <= blob.length();
		     offset += static_cast<qsizetype>(sizeof(GUID)))
		{
			ids.append(guidAt(blob, offset));
		}
	}

	QByteArray currentBytes;
	HKEY sessionKey = nullptr;
	auto sessionPath = QString::fromWCharArray(SESSION_KEY_FORMAT).arg(this->sessionId).toStdWString();
	if (RegOpenKeyExW(HKEY_CURRENT_USER, sessionPath.c_str(), 0, KEY_READ, &sessionKey)
	    == ERROR_SUCCESS)
	{
		readBinary(sessionKey, L"CurrentVirtualDesktop", currentBytes);
		RegCloseKey(sessionKey);
	}

	if (currentBytes.length() < static_cast<qsizetype>(sizeof(GUID))) {
		readBinary(key, L"CurrentVirtualDesktop", currentBytes);
	}

	if (currentBytes.length() >= static_cast<qsizetype>(sizeof(GUID))) {
		current = guidAt(currentBytes, 0);
	}

	RegCloseKey(key);
	return true;
}

void VirtualDesktops::noteCloakChange() {
	if (this->listener != nullptr && this->accessor.loaded) return;
	if (!this->cloakTimer.isActive()) this->cloakTimer.start();
}

void VirtualDesktops::refresh(bool rereadNames) {
	if (this->own || this->ext) return;   // our own / komorebi's list is the truth; the registry describes Windows' desktops

	QList<GUID> ids;
	GUID current {};

	if (!this->readRegistry(ids, current)) {
		qCWarning(logDesktops) << "Unable to read the virtual desktop registry keys.";
	}

	const auto& a = this->accessor;
	if (a.loaded) {
		auto count = this->call(-1, a.getDesktopCount);
		if (count > 0) {
			ids.resize(count);
			if (a.getDesktopIdByNumber != nullptr) {
				for (auto i = 0; i < count; i++) {
					if (guidIsNull(ids[i])) ids[i] = this->call(GUID {}, a.getDesktopIdByNumber, i);
				}
			}
		}
	}

	if (ids.isEmpty()) ids.append(GUID {});

	auto sameIds = !rereadNames && ids.length() == this->mDesktops.length();
	for (qsizetype i = 0; sameIds && i < ids.length(); i++) {
		sameIds = IsEqualGUID(ids[i], this->mDesktops[i].id);
	}

	QList<Desktop> desktops;
	desktops.reserve(ids.length());

	for (qsizetype i = 0; i < ids.length(); i++) {
		auto name = sameIds ? this->mDesktops[i].name : this->readDesktopName(ids[i]);
		desktops.append(Desktop {.id = ids[i], .name = name});
	}

	auto currentIndex = qsizetype(-1);
	for (qsizetype i = 0; i < desktops.length(); i++) {
		if (IsEqualGUID(desktops[i].id, current)) {
			currentIndex = i;
			break;
		}
	}

	if (a.loaded) {
		auto number = this->call(-1, a.getCurrentDesktopNumber);
		if (number >= 0 && number < desktops.length()) {
			currentIndex = number;
			current = desktops[number].id;
		}
	}

	if (currentIndex == -1) currentIndex = 0;

	auto listChanged = desktops.length() != this->mDesktops.length();
	if (!listChanged) {
		for (qsizetype i = 0; i < desktops.length(); i++) {
			if (!IsEqualGUID(desktops[i].id, this->mDesktops[i].id)
			    || desktops[i].name != this->mDesktops[i].name)
			{
				listChanged = true;
				break;
			}
		}
	}

	auto currentChanged = currentIndex != this->mCurrent || !IsEqualGUID(current, this->mCurrentId);

	this->mDesktops = desktops;
	this->mCurrent = currentIndex;
	this->mCurrentId = current;

	if (listChanged) {
		qCDebug(logDesktops) << "Desktop list changed:" << desktops.length() << "desktops";
		emit this->desktopsChanged();
	}

	if (currentChanged) {
		qCDebug(logDesktops) << "Current desktop:" << currentIndex;
		emit this->currentChanged();
	}
}

qsizetype VirtualDesktops::indexOf(const GUID& id) const {
	if (guidIsNull(id)) return -1;

	for (qsizetype i = 0; i < this->mDesktops.length(); i++) {
		if (IsEqualGUID(this->mDesktops[i].id, id)) return i;
	}

	return -1;
}

IVirtualDesktopManager* VirtualDesktops::manager() const {
	if (this->managerTried) return this->mManager;
	this->managerTried = true;

	static constexpr GUID clsid = {
	    0xaa509086,
	    0x5ca9,
	    0x4c25,
	    {0x8f, 0x95, 0x58, 0x9d, 0x3c, 0x07, 0xb4, 0x8a}
	};

	auto hr = CoCreateInstance(
	    clsid,
	    nullptr,
	    CLSCTX_ALL,
	    __uuidof(IVirtualDesktopManager),
	    reinterpret_cast<void**>(&this->mManager)
	);

	if (FAILED(hr)) {
		qCWarning(logDesktops) << "IVirtualDesktopManager unavailable:" << Qt::hex << hr;
		this->mManager = nullptr;
	}

	return this->mManager;
}

GUID VirtualDesktops::windowDesktopId(HWND hwnd) const {
	GUID id {};
	auto* manager = this->manager();
	if (manager == nullptr || hwnd == nullptr) return id;
	if (FAILED(manager->GetWindowDesktopId(hwnd, &id))) return GUID {};
	return id;
}

qsizetype VirtualDesktops::windowDesktopIndex(HWND hwnd) const {
	if (this->ext) return this->extOf.value(hwnd, this->mCurrent);
	if (this->own) {
		if (hwnd == nullptr) return -1;
		if (this->ownPinned.contains(hwnd)) return -1;
		auto iter = this->ownOf.find(hwnd);
		if (iter != this->ownOf.end()) return qMin(iter.value(), this->count() - 1);
		// a new window opens on the workspace you are on
		this->ownOf.insert(hwnd, this->mCurrent);
		SetPropW(hwnd, OWN_WORKSPACE_PROP, reinterpret_cast<HANDLE>(static_cast<quintptr>(this->mCurrent + 1))); // NOLINT
		return this->mCurrent;
	}

	auto index = this->indexOf(this->windowDesktopId(hwnd));
	if (index != -1) return index;

	const auto& a = this->accessor;
	if (a.loaded && a.getWindowDesktopNumber != nullptr) {
		auto number = this->call(-1, a.getWindowDesktopNumber, hwnd);
		if (number >= 0 && number < this->count()) return number;
	}

	return -1;
}

bool VirtualDesktops::isWindowOnCurrent(HWND hwnd) const {
	if (this->ext) return this->extOf.value(hwnd, this->mCurrent) == this->mCurrent;
	if (this->own) {
		auto index = this->windowDesktopIndex(hwnd);
		if (index <= -2) return !this->mShownSpecial.isEmpty() && this->specialName(index) == this->mShownSpecial;
		return index == -1 || index == this->mCurrent;
	}

	auto* manager = this->manager();
	if (manager == nullptr || hwnd == nullptr) return true;
	BOOL onCurrent = TRUE;
	if (FAILED(manager->IsWindowOnCurrentVirtualDesktop(hwnd, &onCurrent))) return true;
	return onCurrent != FALSE;
}

void VirtualDesktops::sendShortcut(WORD key, int times) const {
	hotkeys::KeyboardHook::sendChord(
	    static_cast<uint8_t>(hotkeys::ModCtrl | hotkeys::ModSuper),
	    static_cast<uint8_t>(key),
	    times
	);
}

bool VirtualDesktops::switchTo(qsizetype index) {
	if (index < 0 || index >= this->count()) return false;
	if (this->ext) {
		this->runKomorebic({"focus-workspace", QString::number(index)});
		return true;
	}
	if (index == this->mCurrent) return true;

	if (this->own) {
		auto* foreground = GetForegroundWindow();
		if (this->ownOf.value(foreground, -2) == this->mCurrent) this->ownLastFocus.insert(this->mCurrent, foreground);

		for (auto iter = this->ownOf.begin(); iter != this->ownOf.end();) {
			if (!IsWindow(iter.key())) {
				this->ownHidden.remove(iter.key());
				iter = this->ownOf.erase(iter);
			} else {
				++iter;
			}
		}

		auto old = this->mCurrent;
		this->mCurrent = index;
		this->mCurrentId = ownGuid(index);

		// show the new workspace first, so the screen is never empty in between
		for (auto iter = this->ownOf.cbegin(); iter != this->ownOf.cend(); ++iter) {
			if (iter.value() == index) this->showOwn(iter.key(), true);
		}
		for (auto iter = this->ownOf.cbegin(); iter != this->ownOf.cend(); ++iter) {
			if (iter.value() == old && !this->ownPinned.contains(iter.key())) this->showOwn(iter.key(), false);
		}

		auto* last = this->ownLastFocus.value(index, nullptr);
		if (last != nullptr && IsWindow(last) && this->ownOf.value(last, -2) == index) SetForegroundWindow(last);

		qCDebug(logDesktops) << "Workspace" << index + 1;
		emit this->currentChanged();
		return true;
	}

	if (this->accessor.loaded) {
		if (this->call(-1, this->accessor.goToDesktopNumber, static_cast<int>(index)) == -1) {
			qCWarning(logDesktops) << "GoToDesktopNumber" << index << "failed";
			return false;
		}

		this->refresh();
		return true;
	}

	auto delta = index - this->mCurrent;
	this->sendShortcut(delta > 0 ? VK_RIGHT : VK_LEFT, static_cast<int>(qAbs(delta)));
	return true;
}

void VirtualDesktops::ensureCount(qsizetype count, std::function<void(bool)> done) {
	if (this->ext) {   // komorebi's workspaces come from its config
		done(this->count() >= count);
		return;
	}
	if (count > MAX_DESKTOPS) {
		qCWarning(logDesktops) << "Refusing to create more than" << MAX_DESKTOPS << "desktops";
		done(false);
		return;
	}

	if (this->count() >= count) {
		done(true);
		return;
	}

	if (this->own) {
		this->mDesktops = this->ownList(count);
		emit this->desktopsChanged();
		done(true);
		return;
	}

	this->creationWaiters.append({count, std::move(done)});
	auto idle = this->creationTarget == 0;
	this->creationTarget = std::max(this->creationTarget, count);
	if (idle) this->createNext();
}

void VirtualDesktops::createNext() {
	if (this->count() >= this->creationTarget) {
		this->finishCreation();
		return;
	}

	this->creationBefore = this->count();
	this->creationPolls = 0;

	if (this->accessor.loaded && this->accessor.createDesktop != nullptr) {
		if (this->call(-1, this->accessor.createDesktop) == -1) {
			qCWarning(logDesktops) << "CreateDesktop failed";
			this->finishCreation();
			return;
		}
	} else {
		this->sendShortcut('D', 1);
	}

	this->creationTimer.start();
}

void VirtualDesktops::pollCreation() {
	this->refresh();

	if (this->count() > this->creationBefore) {
		this->creationTimer.stop();
		this->createNext();
		return;
	}

	if (++this->creationPolls >= CREATION_POLLS) {
		this->creationTimer.stop();
		qCWarning(logDesktops) << "Desktop creation did not show up in the registry";
		this->finishCreation();
	}
}

void VirtualDesktops::finishCreation() {
	auto waiters = std::exchange(this->creationWaiters, {});
	this->creationTarget = 0;

	for (auto& [count, done]: waiters) done(this->count() >= count);
}

bool VirtualDesktops::removeDesktop(qsizetype index, qsizetype fallback) {
	const auto& a = this->accessor;
	if (!a.loaded || a.removeDesktop == nullptr) {
		qCWarning(logDesktops) << "Removing a desktop needs VirtualDesktopAccessor.dll";
		return false;
	}

	if (index < 0 || index >= this->count() || fallback < 0 || fallback >= this->count()
	    || index == fallback || this->count() < 2)
	{
		return false;
	}

	if (this->call(-1, a.removeDesktop, static_cast<int>(index), static_cast<int>(fallback)) == -1) {
		qCWarning(logDesktops) << "RemoveDesktop" << index << "failed";
		return false;
	}

	this->refresh();
	return true;
}

bool VirtualDesktops::moveWindow(HWND hwnd, qsizetype index) {
	if (this->ext) {
		// komorebic moves the focused window
		if (hwnd != GetForegroundWindow() || index < 0) return false;
		this->runKomorebic({"move-to-workspace", QString::number(index)});
		return true;
	}
	if (hwnd == nullptr || index < 0 || index >= this->count()) return false;

	if (this->own) {
		this->ownOf.insert(hwnd, index);
		SetPropW(hwnd, OWN_WORKSPACE_PROP, reinterpret_cast<HANDLE>(static_cast<quintptr>(index + 1))); // NOLINT
		if (auto atom = reinterpret_cast<quintptr>(RemovePropW(hwnd, OWN_SPECIAL_PROP)); atom != 0) {
			GlobalDeleteAtom(static_cast<ATOM>(atom));
		}
		auto visible = index == this->mCurrent || this->ownPinned.contains(hwnd);
		this->showOwn(hwnd, visible);
		return true;
	}

	if (isOwnWindow(hwnd)) {
		if (auto* manager = this->manager()) {
			return SUCCEEDED(manager->MoveWindowToDesktop(hwnd, this->mDesktops[index].id));
		}
	}

	if (this->accessor.loaded) {
		return this->call(-1, this->accessor.moveWindowToDesktopNumber, hwnd, static_cast<int>(index)) != -1;
	}

	qCWarning(logDesktops) << "Moving another application's window to a desktop needs"
	                       << "VirtualDesktopAccessor.dll";
	return false;
}

bool VirtualDesktops::pinWindow(HWND hwnd, bool pinned) {
	if (this->own) {
		if (pinned) {
			this->ownPinned.insert(hwnd);
			SetPropW(hwnd, OWN_PINNED_PROP, reinterpret_cast<HANDLE>(1)); // NOLINT
			this->showOwn(hwnd, true);
		} else {
			this->ownPinned.remove(hwnd);
			RemovePropW(hwnd, OWN_PINNED_PROP);
			this->ownOf.insert(hwnd, this->mCurrent);
			SetPropW(hwnd, OWN_WORKSPACE_PROP, reinterpret_cast<HANDLE>(static_cast<quintptr>(this->mCurrent + 1))); // NOLINT
		}
		return true;
	}

	auto& a = this->accessor;
	if (!a.loaded || a.pinWindow == nullptr || a.unPinWindow == nullptr) return false;
	return this->call(-1, pinned ? a.pinWindow : a.unPinWindow, hwnd) != -1;
}

bool VirtualDesktops::isWindowPinned(HWND hwnd) const {
	if (this->own) return this->ownPinned.contains(hwnd);
	const auto& a = this->accessor;
	if (!a.loaded || a.isPinnedWindow == nullptr) return false;
	return this->call(-1, a.isPinnedWindow, hwnd) == 1;
}

// ---------------------------------------------------------------- Caelestia's own workspaces

GUID VirtualDesktops::ownGuid(qsizetype index) {
	return GUID {0xCAE1E571, 0x5EAC, 0x4C1A, {0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, static_cast<BYTE>(index + 1)}};
}

QList<VirtualDesktops::Desktop> VirtualDesktops::ownList(qsizetype count) const {
	QList<Desktop> list;
	for (qsizetype i = 0; i < count; i++) list.append(Desktop {.id = ownGuid(i), .name = QString::number(i + 1)});
	return list;
}

void VirtualDesktops::enableOwn() {
	if (this->ext) return;   // komorebi owns the workspaces
	this->own = true;

	// take back what an earlier run knew: each window's workspace, pins and what it hid
	QList<HWND> all;
	EnumWindows(
	    [](HWND hwnd, LPARAM param) -> BOOL {
		    reinterpret_cast<QList<HWND>*>(param)->append(hwnd); // NOLINT(performance-no-int-to-ptr)
		    return TRUE;
	    },
	    reinterpret_cast<LPARAM>(&all)
	);

	qsizetype highest = 0;
	for (auto* hwnd: all) {
		auto value = reinterpret_cast<quintptr>(GetPropW(hwnd, OWN_WORKSPACE_PROP));
		if (value > 0 && value <= quintptr(MAX_DESKTOPS)) {
			this->ownOf.insert(hwnd, qsizetype(value) - 1);
			highest = qMax(highest, qsizetype(value) - 1);
		}
		if (GetPropW(hwnd, OWN_PINNED_PROP) != nullptr) this->ownPinned.insert(hwnd);
		if (auto atom = reinterpret_cast<quintptr>(GetPropW(hwnd, OWN_SPECIAL_PROP)); atom != 0) {
			wchar_t name[256] {};
			if (GlobalGetAtomNameW(static_cast<ATOM>(atom), name, 256) > 0) {
				auto special = QString::fromWCharArray(name);
				if (!this->specials.contains(special)) this->specials.append(special);
				this->ownOf.insert(hwnd, -2 - this->specials.indexOf(special));
			}
		}
	}
	for (auto* hwnd: std::exchange(this->leftHidden, {})) {
		if (IsWindow(hwnd)) this->ownHidden.insert(hwnd, {});
	}

	this->mDesktops = this->ownList(highest + 1);
	this->mCurrent = 0;
	this->mCurrentId = ownGuid(0);

	// workspace 1 is shown after a (re)start; everything else stays hidden
	for (auto* hwnd: QList(this->ownHidden.keys())) {
		auto index = this->ownOf.value(hwnd, 0);
		if (index == 0 || this->ownPinned.contains(hwnd)) this->showOwn(hwnd, true);
	}
	for (auto iter = this->ownOf.cbegin(); iter != this->ownOf.cend(); ++iter) {
		if (iter.value() != 0 && !this->ownPinned.contains(iter.key())) this->showOwn(iter.key(), false);
	}

	if (auto* app = QCoreApplication::instance()) {
		QObject::connect(app, &QCoreApplication::aboutToQuit, this, [this]() { this->showAllOwn(); });
	}

	qCInfo(logDesktops) << "Windows' virtual desktops are not running (no explorer.exe); using Caelestia's own"
	                    << "workspaces," << this->ownOf.size() << "windows remembered";
	emit this->desktopsChanged();
	emit this->currentChanged();
}

bool VirtualDesktops::hidesWindow(HWND hwnd) const {
	if (!this->own) return false;
	if (this->ownHidden.contains(hwnd)) return true;
	for (const auto& owned: this->ownHidden) {
		if (owned.contains(hwnd)) return true;
	}
	return false;
}

void VirtualDesktops::showOwn(HWND hwnd, bool show) {
	if (show) {
		auto iter = this->ownHidden.find(hwnd);
		if (iter == this->ownHidden.end()) return;
		auto owned = iter.value();
		this->ownHidden.erase(iter);
		RemovePropW(hwnd, OWN_HIDDEN_PROP);
		ShowWindowAsync(hwnd, SW_SHOWNA);
		for (auto* popup: owned) {
			if (IsWindow(popup)) ShowWindowAsync(popup, SW_SHOWNA);
		}
		return;
	}

	if (this->ownHidden.contains(hwnd) || !IsWindowVisible(hwnd)) return;

	// its open dialogs go with it
	struct Search {
		HWND owner;
		QList<HWND> found;
	} search {hwnd, {}};
	EnumWindows(
	    [](HWND candidate, LPARAM param) -> BOOL {
		    auto* s = reinterpret_cast<Search*>(param); // NOLINT(performance-no-int-to-ptr)
		    if (GetWindow(candidate, GW_OWNER) == s->owner && IsWindowVisible(candidate)) s->found.append(candidate);
		    return TRUE;
	    },
	    reinterpret_cast<LPARAM>(&search)
	);

	// marked before it disappears, so the window tracker keeps it
	this->ownHidden.insert(hwnd, search.found);
	SetPropW(hwnd, OWN_HIDDEN_PROP, reinterpret_cast<HANDLE>(1)); // NOLINT
	for (auto* popup: search.found) ShowWindowAsync(popup, SW_HIDE);
	ShowWindowAsync(hwnd, SW_HIDE);
}

// ---------------------------------------------------------------- Aether tiling: komorebi owns the workspaces

namespace {

constexpr const char* KOMOREBI_PIPE = "caelestia-komorebi";

bool processRunning(const wchar_t* exe) {
	auto* snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return false;
	PROCESSENTRY32W entry {};
	entry.dwSize = sizeof(entry);
	auto found = false;
	for (auto ok = Process32FirstW(snap, &entry); ok && !found; ok = Process32NextW(snap, &entry)) {
		found = _wcsicmp(entry.szExeFile, exe) == 0;
	}
	CloseHandle(snap);
	return found;
}

// ends every process with that exe name, and waits a moment for them to go
void killProcesses(const wchar_t* exe) {
	auto* snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return;
	PROCESSENTRY32W entry {};
	entry.dwSize = sizeof(entry);
	for (auto ok = Process32FirstW(snap, &entry); ok; ok = Process32NextW(snap, &entry)) {
		if (_wcsicmp(entry.szExeFile, exe) != 0) continue;
		if (auto* proc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID)) {
			TerminateProcess(proc, 0);
			WaitForSingleObject(proc, 2000);
			CloseHandle(proc);
		}
	}
	CloseHandle(snap);
}

// no console window flashes for komorebic / whkd
void spawnHidden(const QString& program, const QStringList& args) {
	auto line = '"' + QDir::toNativeSeparators(program) + '"';
	for (const auto& arg: args) line += ' ' + (arg.contains(' ') ? '"' + arg + '"' : arg);
	auto wide = line.toStdWString();
	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION info {};
	if (CreateProcessW(nullptr, wide.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
		CloseHandle(info.hThread);
		CloseHandle(info.hProcess);
	} else {
		qCWarning(logDesktops) << "Could not start" << line << "- error" << GetLastError();
	}
}

// komorebi itself, by full path. "komorebic start" runs PowerShell's Start-Process 'komorebi.exe', which resolves
// through PATH: with an official komorebi installed (C:\Program Files\komorebi\bin on PATH) that one starts, fails
// on --hyprland, and komorebic's fallback then runs the fork's komorebi without any flags - no Hyprland mode, no
// workspaces, no IPC (2026-11-15). Started directly, the fork's own exe always gets its flags.
// AetherWM (the WM grown from the komorebi fork) builds as aetherwm.exe / aetherwmc.exe; older builds of the fork
// are still komorebi.exe / komorebic.exe. A distinct name also keeps an official komorebi from being mistaken for it.
QString wmServer(const QString& dir) {
	return QFileInfo::exists(dir + "/aetherwm.exe") ? dir + "/aetherwm.exe" : dir + "/komorebi.exe";
}
QString wmClient(const QString& dir) {
	return QFileInfo::exists(dir + "/aetherwmc.exe") ? dir + "/aetherwmc.exe" : dir + "/komorebic.exe";
}
std::wstring wmProc(const QString& dir) { return QFileInfo(wmServer(dir)).fileName().toStdWString(); }

std::wstring komorebiStartLine(const QString& dir, bool hyprland, bool hyprlandAlt) {
	auto line = L"\"" + QDir::toNativeSeparators(wmServer(dir)).toStdWString() + L"\"";
	if (hyprland) line += L" --hyprland";
	if (hyprland && hyprlandAlt) line += L" --hyprland-alt";
	return line;
}

QJsonArray elementsOf(const QJsonValue& ring) { return ring.toObject().value("elements").toArray(); }

} // namespace

void VirtualDesktops::setKomorebiHyprland(bool hyprland, bool hyprlandAlt, const QString& whkd) {
	if (this->komorebiHyprland == hyprland && (!hyprland || this->komorebiHyprlandAlt == hyprlandAlt)) {
		this->komorebiHyprlandAlt = hyprlandAlt;
		return;
	}
	this->komorebiHyprland = hyprland;
	this->komorebiHyprlandAlt = hyprlandAlt;
	if (!this->ext || this->komorebiDir.isEmpty()) return;

	// stop komorebi, wait for it, start it again in the other mode - off the UI thread (komorebic stop waits for
	// komorebi, which may take a moment or hang)
	const auto komorebic = QDir::toNativeSeparators(wmClient(this->komorebiDir)).toStdWString();
	const auto wmName = wmProc(this->komorebiDir);
	const auto whkdPath = QDir::toNativeSeparators(whkd).toStdWString();
	const auto startLine = komorebiStartLine(this->komorebiDir, hyprland, hyprlandAlt);
	// one restart at a time, and only the latest request runs (quick toggles used to race and leave the wrong mode)
	static std::mutex restartLock;
	static std::atomic<int> restartGeneration {0};
	const int generation = ++restartGeneration;
	std::thread([komorebic, wmName, whkdPath, startLine, hyprland, generation] {
		const std::lock_guard guard(restartLock);
		if (generation != restartGeneration.load()) return;
		auto run = [](std::wstring cmdline, DWORD timeout) {
			STARTUPINFOW si {};
			si.cb = sizeof(si);
			PROCESS_INFORMATION pi {};
			if (!CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return;
			WaitForSingleObject(pi.hProcess, timeout);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
		};
		run(L"\"" + komorebic + L"\" stop", 15000);
		for (int i = 0; i < 40 && processRunning(wmName.c_str()); i++) Sleep(250);
		run(startLine, 0);
		if (hyprland) {
			if (processRunning(L"whkd.exe")) killProcesses(L"whkd.exe");
		} else if (!whkdPath.empty() && !processRunning(L"whkd.exe")) {
			run(L"\"" + whkdPath + L"\"", 0);
		}
	}).detach();
}

void VirtualDesktops::runKomorebic(const QStringList& args) const {
	spawnHidden(wmClient(this->komorebiDir), args);
}

void VirtualDesktops::setKomorebi(const QString& dir, const QString& whkd, bool hyprland, bool hyprlandAlt) {
	this->komorebiHyprland = hyprland;
	this->komorebiHyprlandAlt = hyprlandAlt;
	if (dir.isEmpty()) {
		if (!this->ext) return;
		this->ext = false;
		this->komorebiRetry.stop();
		if (this->komorebiServer != nullptr) {
			this->komorebiServer->close();
			this->komorebiServer->deleteLater();
			this->komorebiServer = nullptr;
		}
		this->extOf.clear();
		this->refresh(true);
		return;
	}

	this->komorebiDir = dir;
	if (!QFileInfo::exists(wmClient(dir))) {
		qCWarning(logDesktops) << "aetherwmc.exe / komorebic.exe not found in" << dir << "- Aether tiling stays off";
		return;
	}

	// Caelestia's own workspaces would fight komorebi's: give back everything they hid
	if (this->own) {
		this->showAllOwn();
		this->own = false;
		this->ownOf.clear();
		this->ownPinned.clear();
	}
	this->ext = true;

	// start komorebi and the hotkey daemon with the shell
	if (!processRunning(wmProc(dir).c_str())) {
		auto line = komorebiStartLine(dir, hyprland, hyprlandAlt);
		STARTUPINFOW si {};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi {};
		if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
		} else {
			qCWarning(logDesktops) << "Could not start komorebi from" << dir << "- error" << GetLastError();
		}
	}
	// Hyprland mode: komorebi handles hyprland.conf's binds itself - a whkd as well would double them up
	if (hyprland && processRunning(L"whkd.exe")) killProcesses(L"whkd.exe");
	if (!hyprland && !whkd.isEmpty() && QFileInfo::exists(whkd)) {
		// whkd's binds run "komorebic ...": it must find this komorebic (the fork's commands, eg. toggle-overview,
		// are missing from an official komorebic on the system PATH), so this folder goes first in the PATH it
		// inherits from the shell
		const auto native = QDir::toNativeSeparators(dir);
		const auto path = qEnvironmentVariable("PATH");
		if (!path.startsWith(native + ';', Qt::CaseInsensitive))
			SetEnvironmentVariableW(L"PATH", (native + ';' + path).toStdWString().c_str());
		// a fresh whkd every time the shell starts: one left over from before may have lost its keyboard hook
		// (Windows silently removes a hook that stalls once), and then none of its hotkeys work
		if (processRunning(L"whkd.exe")) killProcesses(L"whkd.exe");
		spawnHidden(whkd, {});
	}

	if (this->komorebiServer == nullptr) {
		this->komorebiServer = new QLocalServer(this);
		QLocalServer::removeServer(KOMOREBI_PIPE);
		if (!this->komorebiServer->listen(KOMOREBI_PIPE)) {
			qCWarning(logDesktops) << "Could not listen on" << KOMOREBI_PIPE << this->komorebiServer->errorString();
		}
		QObject::connect(this->komorebiServer, &QLocalServer::newConnection, this, [this]() {
			while (auto* socket = this->komorebiServer->nextPendingConnection()) {
				this->komorebiRetry.stop();
				this->komorebiBuffer.clear();
				QObject::connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
					this->komorebiBuffer += socket->readAll();
					qsizetype end = 0;
					while ((end = this->komorebiBuffer.indexOf('\n')) != -1) {
						auto line = this->komorebiBuffer.left(end);
						this->komorebiBuffer.remove(0, end + 1);
						if (!line.trimmed().isEmpty()) this->komorebiState(line);
					}
				});
				// komorebi restarted (or stopped): subscribe again once it is back
				QObject::connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
					socket->deleteLater();
					if (this->ext) this->komorebiRetry.start();
				});
			}
		});

		this->komorebiRetry.setInterval(3000);
		QObject::connect(&this->komorebiRetry, &QTimer::timeout, this, [this]() { this->komorebiSubscribe(); });
	}

	// komorebi needs a moment after starting; keep asking until it connects
	QTimer::singleShot(1500, this, [this]() { this->komorebiSubscribe(); });
	this->komorebiRetry.start();
}

void VirtualDesktops::komorebiSubscribe() {
	if (!this->ext) return;
	if (!processRunning(wmProc(this->komorebiDir).c_str())) return;
	runKomorebic({"subscribe-pipe", KOMOREBI_PIPE});
}

void VirtualDesktops::komorebiState(const QByteArray& json) {
	auto doc = QJsonDocument::fromJson(json);
	auto state = doc.object().value("state").toObject();
	if (state.isEmpty()) return;

	auto monitorsRing = state.value("monitors").toObject();
	auto monitors = monitorsRing.value("elements").toArray();
	auto focusedMonitor = monitorsRing.value("focused").toInt();
	if (monitors.isEmpty()) return;

	QHash<HWND, qsizetype> map;
	for (const auto& monitorValue: monitors) {
		auto workspaces = elementsOf(monitorValue.toObject().value("workspaces"));
		for (qsizetype i = 0; i < workspaces.size(); i++) {
			auto ws = workspaces.at(i).toObject();
			auto add = [&map, i](const QJsonArray& windows) {
				for (const auto& w: windows) {
					auto hwnd = reinterpret_cast<HWND>(static_cast<quintptr>(w.toObject().value("hwnd").toInteger())); // NOLINT
					map.insert(hwnd, i);
				}
			};
			for (const auto& container: elementsOf(ws.value("containers"))) {
				add(elementsOf(container.toObject().value("windows")));
			}
			add(elementsOf(ws.value("floating_windows")));
			auto monocle = ws.value("monocle_container");
			if (monocle.isObject()) add(elementsOf(monocle.toObject().value("windows")));
		}
	}

	auto monitor = monitors.at(qBound(0, focusedMonitor, int(monitors.size()) - 1)).toObject();
	auto workspacesRing = monitor.value("workspaces").toObject();
	auto workspaces = workspacesRing.value("elements").toArray();
	auto current = qsizetype(workspacesRing.value("focused").toInt());

	QList<Desktop> list;
	for (qsizetype i = 0; i < workspaces.size(); i++) {
		auto name = workspaces.at(i).toObject().value("name").toString();
		list.append(Desktop {.id = ownGuid(i), .name = name.isEmpty() ? QString::number(i + 1) : name});
	}

	auto listChanged = list.size() != this->mDesktops.size();
	for (qsizetype i = 0; !listChanged && i < list.size(); i++) listChanged = list[i].name != this->mDesktops[i].name;
	auto mapChanged = map != this->extOf;
	auto currentChanged = current != this->mCurrent;

	this->mDesktops = list;
	this->extOf = map;
	this->mCurrent = current;
	this->mCurrentId = ownGuid(current);

	if (listChanged || mapChanged) emit this->desktopsChanged();
	if (currentChanged) emit this->currentChanged();
}

QString VirtualDesktops::specialName(qsizetype index) const { return this->specials.value(-2 - index); }

bool VirtualDesktops::moveToSpecial(HWND hwnd, const QString& name) {
	if (!this->own || hwnd == nullptr) return false;
	auto special = name.isEmpty() ? QStringLiteral("special") : name;
	if (!this->specials.contains(special)) this->specials.append(special);

	this->ownOf.insert(hwnd, -2 - this->specials.indexOf(special));
	RemovePropW(hwnd, OWN_WORKSPACE_PROP);
	if (auto old = reinterpret_cast<quintptr>(GetPropW(hwnd, OWN_SPECIAL_PROP)); old != 0) {
		GlobalDeleteAtom(static_cast<ATOM>(old));
	}
	auto wide = special.toStdWString();
	SetPropW(hwnd, OWN_SPECIAL_PROP, reinterpret_cast<HANDLE>(static_cast<quintptr>(GlobalAddAtomW(wide.c_str())))); // NOLINT
	this->showOwn(hwnd, this->mShownSpecial == special);
	return true;
}

bool VirtualDesktops::toggleSpecial(const QString& name) {
	if (!this->own) return false;
	auto special = name.isEmpty() ? QStringLiteral("special") : name;

	auto setShown = [this](const QString& which, bool show) {
		auto index = -2 - this->specials.indexOf(which);
		HWND first = nullptr;
		for (auto iter = this->ownOf.cbegin(); iter != this->ownOf.cend(); ++iter) {
			if (iter.value() != index || !IsWindow(iter.key())) continue;
			this->showOwn(iter.key(), show);
			if (first == nullptr) first = iter.key();
		}
		return first;
	};

	if (this->mShownSpecial == special) {
		setShown(special, false);
		this->mShownSpecial.clear();
	} else {
		if (!this->mShownSpecial.isEmpty()) setShown(this->mShownSpecial, false);
		this->mShownSpecial = special;
		if (!this->specials.contains(special)) this->specials.append(special);
		if (auto* first = setShown(special, true)) SetForegroundWindow(first);
	}

	emit this->currentChanged();
	return true;
}

void VirtualDesktops::showAllOwn() {
	for (auto* hwnd: QList(this->ownHidden.keys())) this->showOwn(hwnd, true);
}

QString VirtualDesktops::guidToString(const GUID& guid) {
	wchar_t buffer[40] {};
	StringFromGUID2(guid, buffer, 40);
	return QString::fromWCharArray(buffer);
}

} // namespace qs::windows
