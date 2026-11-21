#include "desktop_drop.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cwchar>

#include <qcoreapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qpointer.h>
#include <qscreen.h>
#include <qstring.h>

#include "desktop_host.hpp"
#include "input_mask.hpp"
#include "util.hpp"

#include <knownfolders.h>
#include <objidl.h>
#include <oleidl.h>
#include <shellapi.h>

namespace qs::windows {

namespace {

Q_LOGGING_CATEGORY(logDesktopDrop, "quickshell.windows.desktopdrop", QtWarningMsg);

constexpr auto MESSAGE_WINDOW_CLASS = L"QuickshellDesktopDrop";
constexpr auto OVERLAY_WINDOW_CLASS = L"QuickshellDesktopDropTarget";

constexpr UINT WM_DD_ARM = WM_APP + 1;

constexpr int ENTER_WAIT_MS = 1200;
constexpr int SAFETY_MS = 60000;

std::atomic<bool> gFeatureEnabled = false; // NOLINT
std::atomic<bool> gTrackingDown = false;   // NOLINT
std::atomic<bool> gDragging = false;       // NOLINT
std::atomic<DWORD> gLastCheck = 0;         // NOLINT
std::atomic<bool> gArmed = false;          // NOLINT
std::atomic<LONG> gDownX = 0;              // NOLINT
std::atomic<LONG> gDownY = 0;              // NOLINT
std::atomic<HWND> gGuiWindow = nullptr;    // NOLINT

bool isDesktopSurface(POINT pt) {
	auto* hit = WindowFromPoint(pt);
	if (hit == nullptr) return false;

	std::array<wchar_t, 64> cls {};
	if (GetClassNameW(hit, cls.data(), static_cast<int>(cls.size())) == 0) return false;

	static constexpr std::array<const wchar_t*, 4> NAMES {
	    L"SysListView32",
	    L"SHELLDLL_DefView",
	    L"WorkerW",
	    L"Progman",
	};

	return std::ranges::any_of(NAMES, [&cls](const wchar_t* name) {
		return std::wcscmp(cls.data(), name) == 0;
	});
}

bool shiftHeld() { return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0; }

FORMATETC hdropFormat() {
	return FORMATETC {
	    .cfFormat = CF_HDROP,
	    .ptd = nullptr,
	    .dwAspect = DVASPECT_CONTENT,
	    .lindex = -1,
	    .tymed = TYMED_HGLOBAL,
	};
}

bool hasHDrop(IDataObject* obj) {
	if (obj == nullptr) return false;
	auto fmt = hdropFormat();
	return obj->QueryGetData(&fmt) == S_OK;
}

QStringList extractDroppedPaths(IDataObject* obj) {
	QStringList paths;
	if (obj == nullptr) return paths;

	auto fmt = hdropFormat();
	STGMEDIUM medium {};
	if (obj->GetData(&fmt, &medium) != S_OK) return paths;

	auto* drop = static_cast<HDROP>(medium.hGlobal);
	auto count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);

	for (UINT i = 0; i < count; i++) {
		auto len = DragQueryFileW(drop, i, nullptr, 0);
		if (len == 0) continue;

		std::wstring buffer(len + 1, L'\0');
		DragQueryFileW(drop, i, buffer.data(), len + 1);
		paths.append(QString::fromWCharArray(buffer.c_str(), static_cast<qsizetype>(len)));
	}

	ReleaseStgMedium(&medium);
	return paths;
}

bool isWallpaperFile(const QStringList& paths) {
	if (paths.size() != 1) return false;
	static const QStringList extensions = {
	    QStringLiteral("png"), QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("webp"),
	    QStringLiteral("bmp"), QStringLiteral("gif"),  QStringLiteral("avif"), QStringLiteral("jxl"),
	    QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("mp4"),  QStringLiteral("webm"),
	    QStringLiteral("mkv"), QStringLiteral("avi"),  QStringLiteral("mov"),  QStringLiteral("m4v"),
	    QStringLiteral("wmv"),
	};
	auto dot = paths.first().lastIndexOf(u'.');
	if (dot < 0) return false;
	return extensions.contains(paths.first().mid(dot + 1).toLower());
}

} // namespace

class DesktopDropTarget final: public IDropTarget {
public:
	explicit DesktopDropTarget(DesktopDropManager* owner): owner(owner) {}

	HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) override {
		if (ppv == nullptr) return E_POINTER;

		if (riid == IID_IUnknown || riid == IID_IDropTarget) {
			*ppv = static_cast<IDropTarget*>(this);
			this->AddRef();
			return S_OK;
		}

		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	ULONG __stdcall AddRef() override {
		return this->refs.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	ULONG __stdcall Release() override {
		auto remaining = this->refs.fetch_sub(1, std::memory_order_acq_rel) - 1;
		if (remaining == 0) delete this;
		return remaining;
	}

	HRESULT __stdcall DragEnter(IDataObject* obj, DWORD /*keyState*/, POINTL /*pt*/, DWORD* effect)
	    override {
		this->hasFiles = hasHDrop(obj) && isWallpaperFile(extractDroppedPaths(obj));
		*effect = this->hasFiles ? DROPEFFECT_COPY : DROPEFFECT_NONE;
		this->owner->notifyDragEntered();
		if (!this->hasFiles) this->owner->requestDisarm();
		return S_OK;
	}

	HRESULT __stdcall DragOver(DWORD /*keyState*/, POINTL /*pt*/, DWORD* effect) override {
		*effect = this->hasFiles ? DROPEFFECT_COPY : DROPEFFECT_NONE;
		return S_OK;
	}

	HRESULT __stdcall DragLeave() override {
		this->owner->requestDisarm();
		return S_OK;
	}

	HRESULT __stdcall Drop(IDataObject* obj, DWORD /*keyState*/, POINTL pt, DWORD* effect) override {
		auto paths = extractDroppedPaths(obj);
		auto accepted = isWallpaperFile(paths);
		*effect = accepted ? DROPEFFECT_COPY : DROPEFFECT_NONE;
		if (accepted) this->owner->notifyDropped(paths, pt);
		this->owner->requestDisarm();
		return S_OK;
	}

private:
	DesktopDropManager* owner;
	std::atomic<ULONG> refs {1};
	bool hasFiles = false;
};

DesktopDropManager* DesktopDropManager::instance() {
	static QPointer<DesktopDropManager> manager; // NOLINT

	if (manager.isNull()) {
		manager = new DesktopDropManager(QCoreApplication::instance());
	}

	return manager.data();
}

DesktopDropManager::DesktopDropManager(QObject* parent): QObject(parent) {
	WNDCLASSW wndClass {};
	wndClass.lpfnWndProc = &DesktopDropManager::messageWindowProc;
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
		qCWarning(logDesktopDrop) << "Failed to create the message window, desktop drops won't work.";
	}

	gGuiWindow.store(this->messageWindow);

	this->enterWaitTimer.setSingleShot(true);
	this->enterWaitTimer.setInterval(ENTER_WAIT_MS);
	QObject::connect(&this->enterWaitTimer, &QTimer::timeout, this, &DesktopDropManager::disarm);

	this->safetyTimer.setSingleShot(true);
	this->safetyTimer.setInterval(SAFETY_MS);
	QObject::connect(&this->safetyTimer, &QTimer::timeout, this, &DesktopDropManager::disarm);
}

DesktopDropManager::~DesktopDropManager() {
	gFeatureEnabled.store(false);
	gGuiWindow.store(nullptr);
	this->disarm();

	if (this->overlay != nullptr) {
		DestroyWindow(this->overlay);
		this->overlay = nullptr;
	}

	if (this->messageWindow != nullptr) {
		DestroyWindow(this->messageWindow);
		this->messageWindow = nullptr;
	}

	if (this->oleInited) OleUninitialize();
}

void DesktopDropManager::setEnabled(bool enabled) {
	if (enabled == this->mEnabled) return;
	this->mEnabled = enabled;
	gFeatureEnabled.store(enabled);

	auto* tracker = InputMaskTracker::instance();
	if (enabled) tracker->acquireHook();
	else {
		tracker->releaseHook();
		this->disarm();
	}

	emit this->enabledChanged();
}

void DesktopDropManager::onMouseHook(WPARAM message, const MSLLHOOKSTRUCT* info) {
	if (!gFeatureEnabled.load(std::memory_order_relaxed)) return;

	if (message == WM_LBUTTONDOWN) {
		gDownX.store(info->pt.x, std::memory_order_relaxed);
		gDownY.store(info->pt.y, std::memory_order_relaxed);
		gDragging.store(false, std::memory_order_relaxed);
		gTrackingDown.store(!isDesktopSurface(info->pt), std::memory_order_relaxed);
		return;
	}

	if (message == WM_LBUTTONUP) {
		gTrackingDown.store(false, std::memory_order_relaxed);
		gDragging.store(false, std::memory_order_relaxed);
		return;
	}

	if (message != WM_MOUSEMOVE) return;
	if (!gTrackingDown.load(std::memory_order_relaxed) || gArmed.load(std::memory_order_relaxed)) {
		return;
	}

	if (!gDragging.load(std::memory_order_relaxed)) {
		auto dx = info->pt.x - gDownX.load(std::memory_order_relaxed);
		auto dy = info->pt.y - gDownY.load(std::memory_order_relaxed);
		if (std::abs(dx) < GetSystemMetrics(SM_CXDRAG) && std::abs(dy) < GetSystemMetrics(SM_CYDRAG)) {
			return;
		}
		gDragging.store(true, std::memory_order_relaxed);
	}

	if (info->time - gLastCheck.load(std::memory_order_relaxed) < 40) return;
	gLastCheck.store(info->time, std::memory_order_relaxed);

	if (!isDesktopSurface(info->pt)) return;
	if (shiftHeld()) return;
	gTrackingDown.store(false, std::memory_order_relaxed);

	gArmed.store(true, std::memory_order_relaxed);
	if (auto* target = gGuiWindow.load()) PostMessageW(target, WM_DD_ARM, 0, 0);
}

void DesktopDropManager::onArmRequested() {
	if (this->armedState) return;

	auto* host = DesktopHost::instance();
	auto* iconsHost = host->active() ? host->iconsHost() : nullptr;

	if (iconsHost == nullptr || !this->ensureOverlay(iconsHost) || !this->registerTarget()) {
		gArmed.store(false, std::memory_order_relaxed);
		return;
	}

	POINT cursor {};
	GetCursorPos(&cursor);
	auto* screen = screenForMonitor(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST));
	this->mActiveScreen = screen != nullptr ? screen->name() : QString();

	this->armedState = true;
	emit this->activeChanged();
	this->enterWaitTimer.start();
	this->safetyTimer.start();
}

bool DesktopDropManager::ensureOverlay(HWND parent) {
	if (this->overlay == nullptr) {
		WNDCLASSW wndClass {};
		wndClass.lpfnWndProc = DefWindowProcW;
		wndClass.hInstance = GetModuleHandleW(nullptr);
		wndClass.lpszClassName = OVERLAY_WINDOW_CLASS;
		RegisterClassW(&wndClass);

		this->overlay = CreateWindowExW(
		    WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		    OVERLAY_WINDOW_CLASS,
		    L"",
		    WS_POPUP,
		    0,
		    0,
		    0,
		    0,
		    nullptr,
		    nullptr,
		    wndClass.hInstance,
		    nullptr
		);

		if (this->overlay == nullptr) {
			qCWarning(logDesktopDrop) << "Could not create the desktop drop overlay window.";
			return false;
		}

		SetLayeredWindowAttributes(this->overlay, 0, 1, LWA_ALPHA);
	}

	auto* desktopRoot = GetAncestor(parent, GA_ROOT);
	auto* above = desktopRoot != nullptr ? GetWindow(desktopRoot, GW_HWNDPREV) : nullptr;
	if (above == this->overlay) above = GetWindow(this->overlay, GW_HWNDPREV);

	SetWindowPos(
	    this->overlay,
	    above != nullptr ? above : HWND_TOP,
	    GetSystemMetrics(SM_XVIRTUALSCREEN),
	    GetSystemMetrics(SM_YVIRTUALSCREEN),
	    GetSystemMetrics(SM_CXVIRTUALSCREEN),
	    GetSystemMetrics(SM_CYVIRTUALSCREEN),
	    SWP_NOACTIVATE | SWP_SHOWWINDOW
	);

	return true;
}

bool DesktopDropManager::registerTarget() {
	if (this->targetRegistered) return true;

	if (!this->oleInited) {
		this->oleInited = SUCCEEDED(OleInitialize(nullptr));
	}

	if (this->dropTarget == nullptr) this->dropTarget = new DesktopDropTarget(this);

	auto hr = RegisterDragDrop(this->overlay, this->dropTarget);
	if (hr == DRAGDROP_E_ALREADYREGISTERED) hr = S_OK;

	this->targetRegistered = SUCCEEDED(hr);
	if (!this->targetRegistered) {
		qCWarning(logDesktopDrop) << "RegisterDragDrop failed, error" << hr;
	}

	return this->targetRegistered;
}

void DesktopDropManager::disarm() {
	this->enterWaitTimer.stop();
	this->safetyTimer.stop();
	gArmed.store(false, std::memory_order_relaxed);

	if (this->targetRegistered && this->overlay != nullptr) {
		RevokeDragDrop(this->overlay);
		this->targetRegistered = false;
	}

	if (this->overlay != nullptr) ShowWindow(this->overlay, SW_HIDE);

	if (!this->armedState) return;
	this->armedState = false;
	this->mActiveScreen.clear();
	emit this->activeChanged();
}

void DesktopDropManager::requestDisarm() {
	QMetaObject::invokeMethod(this, &DesktopDropManager::disarm, Qt::QueuedConnection);
}

void DesktopDropManager::notifyDragEntered() { this->enterWaitTimer.stop(); }

void DesktopDropManager::notifyDropped(const QStringList& paths, POINTL pt) {
	auto* monitor = MonitorFromPoint(POINT {.x = pt.x, .y = pt.y}, MONITOR_DEFAULTTONEAREST);
	auto* screen = screenForMonitor(monitor);
	auto screenName = screen != nullptr ? screen->name() : QString();

	QMetaObject::invokeMethod(
	    this,
	    [this, paths, screenName]() {
		    this->mActiveScreen = screenName;
		    emit this->filesDropped(paths, screenName);
	    },
	    Qt::QueuedConnection
	);
}

LRESULT CALLBACK
DesktopDropManager::messageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_DD_ARM) {
		DesktopDropManager::instance()->onArmRequested();
		return 0;
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

DesktopDrop::DesktopDrop(QObject* parent): QObject(parent) {
	auto* manager = DesktopDropManager::instance();
	QObject::connect(manager, &DesktopDropManager::enabledChanged, this, &DesktopDrop::enabledChanged);
	QObject::connect(manager, &DesktopDropManager::activeChanged, this, &DesktopDrop::activeChanged);
	QObject::connect(manager, &DesktopDropManager::filesDropped, this, &DesktopDrop::filesDropped);
}

bool DesktopDrop::enabled() const { return DesktopDropManager::instance()->enabled(); }
void DesktopDrop::setEnabled(bool enabled) { DesktopDropManager::instance()->setEnabled(enabled); }
bool DesktopDrop::active() const { return DesktopDropManager::instance()->active(); }
QString DesktopDrop::activeScreen() const { return DesktopDropManager::instance()->activeScreen(); }

} // namespace qs::windows
