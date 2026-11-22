#include "panel_window.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>

#include <qt_windows.h>

#include <dwmapi.h>
#include <dxgi.h>
#include <qabstractanimation.h>
#include <qbytearray.h>
#include <qevent.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qqmlengine.h>
#include <qquickwindow.h>
#include <qrect.h>
#include <qregion.h>
#include <qcursor.h>
#include <qeasingcurve.h>
#include <qguiapplication.h>
#include <qscreen.h>
#include <qsgrendererinterface.h>
#include <qtenvironmentvariables.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>
#include <qvariantanimation.h>
#include <rhi/qrhi.h>
#include <shellapi.h>

#include "../core/generation.hpp"
#include "../core/qmlscreen.hpp"
#include "../core/types.hpp"
#include "../window/panelinterface.hpp"
#include "../window/proxywindow.hpp"
#include "appbar.hpp"
#include "blur.hpp"
#include "desktop_host.hpp"
#include "input_mask.hpp"
#include "panel_animation.hpp"
#include "panel_effects.hpp"
#include "startup.hpp"
#include "util.hpp"
#include "virtual_desktops.hpp"
#include "window_manager.hpp"

namespace qs::windows {

namespace {
Q_LOGGING_CATEGORY(logPanel, "quickshell.windows.panel", QtWarningMsg);

void setChildStyle(HWND hwnd, bool child) {
	auto style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
	auto newStyle = child ? ((style & ~static_cast<DWORD>(WS_POPUP)) | WS_CHILD)
	                      : ((style & ~static_cast<DWORD>(WS_CHILD)) | WS_POPUP);
	if (newStyle != style) SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(newStyle));
}

bool forceLayeredDesktopPanels() {
	static const bool force = qEnvironmentVariableIntValue("QS_DESKTOP_LAYERED") != 0;
	return force;
}

constexpr int MAX_RESTACKS = 20;
constexpr DWORD RESTACK_WINDOW_MS = 5000;
constexpr int FOCUS_EXPOSE_FALLBACK_MS = 500;
constexpr int CLOSE_FRAMES = 2;
constexpr int CLOSE_FRAME_TIMEOUT_MS = 150;
constexpr int FIRST_FRAME_TIMEOUT_MS = 250;
constexpr UINT MICROSOFT_VENDOR_ID = 0x1414;

bool systemAnimationsEnabled() {
	BOOL enabled = TRUE;
	if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) == 0) return true;
	return enabled != FALSE;
}

bool primaryAdapterIsSoftware() {
	static const bool software = []() {
		IDXGIFactory1* factory = nullptr;
		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
			return false;
		}

		auto result = false;
		IDXGIAdapter1* adapter = nullptr;

		if (SUCCEEDED(factory->EnumAdapters1(0, &adapter))) {
			DXGI_ADAPTER_DESC1 desc {};
			if (SUCCEEDED(adapter->GetDesc1(&desc))) {
				result = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0
				      || desc.VendorId == MICROSOFT_VENDOR_ID;
			}
			adapter->Release();
		}

		factory->Release();
		return result;
	}();

	return software;
}

std::optional<bool> windowRendersInSoftware(QQuickWindow* window) {
	auto* renderer = window->rendererInterface();
	if (renderer != nullptr && !QSGRendererInterface::isApiRhiBased(renderer->graphicsApi())) {
		return true;
	}

	auto* rhi = window->rhi();
	if (rhi == nullptr) return std::nullopt;

	auto info = rhi->driverInfo();
	return info.deviceType == QRhiDriverInfo::CpuDevice || info.vendorId == MICROSOFT_VENDOR_ID;
}

bool expressiveOnSoftware() {
	static const bool forced = qEnvironmentVariableIntValue("QS_EXPRESSIVE_SOFTWARE") != 0;
	return forced;
}

bool softwareRendering(QQuickWindow* window) {
	if (window != nullptr) {
		if (auto known = windowRendersInSoftware(window)) return *known;
	}

	for (auto* other: QGuiApplication::topLevelWindows()) {
		auto* quick = qobject_cast<QQuickWindow*>(other);
		if (quick == nullptr || quick == window) continue;
		if (auto known = windowRendersInSoftware(quick)) return *known;
	}

	return primaryAdapterIsSoftware();
}

bool ownsForeground(HWND hwnd) {
	auto* current = GetForegroundWindow();

	while (current != nullptr) {
		if (current == hwnd) return true;
		current = GetWindow(current, GW_OWNER);
	}

	return false;
}

HWND nextActivatableWindow(HWND from) {
	for (auto* hwnd = GetWindow(from, GW_HWNDNEXT); hwnd != nullptr;
	     hwnd = GetWindow(hwnd, GW_HWNDNEXT))
	{
		if (!IsWindowVisible(hwnd) || !IsWindowEnabled(hwnd) || IsIconic(hwnd)) continue;

		auto exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		if ((exStyle & (WS_EX_NOACTIVATE | WS_EX_TRANSPARENT)) != 0) continue;
		if ((exStyle & WS_EX_TOOLWINDOW) != 0 && (exStyle & WS_EX_APPWINDOW) == 0) continue;

		BOOL cloaked = FALSE;
		if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
		    && cloaked != FALSE)
		{
			continue;
		}

		RECT rect {};
		if (!GetWindowRect(hwnd, &rect) || IsRectEmpty(&rect)) continue;

		return hwnd;
	}

	return nullptr;
}

bool overlaps(HWND a, HWND b) {
	RECT ra {};
	RECT rb {};
	RECT shared {};
	if (!GetWindowRect(a, &ra) || !GetWindowRect(b, &rb)) return true;
	return IntersectRect(&shared, &ra, &rb) != FALSE;
}

bool onTopOfOthers(HWND hwnd, QList<HWND>& ownAbove) {
	if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0) return false;

	for (auto* above = GetWindow(hwnd, GW_HWNDPREV); above != nullptr;
	     above = GetWindow(above, GW_HWNDPREV))
	{
		if (isOwnProcessWindow(above)) ownAbove.append(above);
		else if (IsWindowVisible(above)) return false;
	}

	return true;
}

template <typename Pred>
bool onlyBelow(HWND hwnd, Pred allowed) {
	for (auto* below = GetWindow(hwnd, GW_HWNDNEXT); below != nullptr;
	     below = GetWindow(below, GW_HWNDNEXT))
	{
		if (!allowed(below)) return false;
	}

	return true;
}

bool positionUnchanged(const WINDOWPOS* pos) {
	constexpr UINT unchanged = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER;
	constexpr UINT shownOrHidden = SWP_SHOWWINDOW | SWP_HIDEWINDOW;
	return pos != nullptr && (pos->flags & unchanged) == unchanged
	    && (pos->flags & shownOrHidden) == 0;
}

QRegion toPhysicalRegion(const QRegion& region, qreal dpr) {
	QRegion physical;

	for (const auto& rect: region) {
		auto left = static_cast<int>(std::floor(rect.x() * dpr));
		auto top = static_cast<int>(std::floor(rect.y() * dpr));
		auto right = static_cast<int>(std::ceil((rect.x() + rect.width()) * dpr));
		auto bottom = static_cast<int>(std::ceil((rect.y() + rect.height()) * dpr));
		physical += QRect(left, top, right - left, bottom - top);
	}

	return physical;
}

HRGN toHrgn(const QRegion& region) {
	auto* hrgn = CreateRectRgn(0, 0, 0, 0);

	for (const auto& rect: region) {
		auto* part = CreateRectRgn(rect.x(), rect.y(), rect.x() + rect.width(), rect.y() + rect.height());
		CombineRgn(hrgn, hrgn, part, RGN_OR);
		DeleteObject(part);
	}

	return hrgn;
}
} // namespace

// Is the foreground window another app covering its whole monitor (a game, a fullscreen video)? Panels on the
// Top layer then go below it. Windows only tells AppBars this (ABN_FULLSCREENAPP), and registering one needs
// Explorer's taskbar - without Explorer every registration fails, so bars stayed over fullscreen games
// (2026-11-08, Roblox). This check needs nothing from Explorer.
bool foregroundAppIsFullscreen() {
	HWND fg = GetForegroundWindow();
	if (fg == nullptr || !IsWindowVisible(fg) || IsIconic(fg) || isOwnProcessWindow(fg)) return false;

	wchar_t cls[32] {};
	GetClassNameW(fg, cls, 32);
	if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 || wcscmp(cls, L"Shell_TrayWnd") == 0) {
		return false; // the desktop
	}

	// screen-sized windows that are not an app on screen: they stayed foreground after a screenshot (the snipping
	// overlay is cloaked, not destroyed) or after leaving fullscreen, and the bars stayed under everything until a
	// key gave Aether the focus (2026-11-21). Cloaked, click-through, no-activate and tool windows are overlays.
	BOOL cloaked = FALSE;
	if (SUCCEEDED(DwmGetWindowAttribute(fg, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
	auto exStyle = GetWindowLongPtrW(fg, GWL_EXSTYLE);
	if ((exStyle & (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)) != 0) return false;
	if ((exStyle & WS_EX_LAYERED) != 0) {
		BYTE alpha = 255;
		DWORD layeredFlags = 0;
		if (GetLayeredWindowAttributes(fg, nullptr, &alpha, &layeredFlags) && (layeredFlags & LWA_ALPHA) != 0 && alpha == 0) {
			return false;
		}
	}
	// another Quickshell (a second shell, its wallpaper / backdrop) or AetherWM's own windows are not a game
	{
		DWORD pid = 0;
		GetWindowThreadProcessId(fg, &pid);
		if (auto* process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
			wchar_t image[MAX_PATH] {};
			DWORD length = MAX_PATH;
			auto named = QueryFullProcessImageNameW(process, 0, image, &length);
			CloseHandle(process);
			if (named) {
				const auto* name = wcsrchr(image, L'\\');
				name = name != nullptr ? name + 1 : image;
				if (_wcsicmp(name, L"qsw.exe") == 0 || _wcsicmp(name, L"qs.exe") == 0 || _wcsicmp(name, L"aetherwm.exe") == 0
				    || _wcsicmp(name, L"komorebi.exe") == 0 || _wcsicmp(name, L"ScreenClippingHost.exe") == 0)
				{
					return false;
				}
			}
		}
	}

	RECT rect {};
	MONITORINFO info {};
	info.cbSize = sizeof(info);
	if (!GetWindowRect(fg, &rect) || !GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &info)) {
		return false;
	}
	// maximized windows stop at the work area (the bars' reserved edges), so they do not count
	return rect.left <= info.rcMonitor.left && rect.top <= info.rcMonitor.top && rect.right >= info.rcMonitor.right
	    && rect.bottom >= info.rcMonitor.bottom;
}

class WinPanelStack {
public:
	static WinPanelStack* instance() {
		static WinPanelStack* stack = nullptr; // NOLINT

		if (stack == nullptr) {
			stack = new WinPanelStack();

			auto* fullscreenPoll = new QTimer(VirtualDesktops::instance());
			fullscreenPoll->setInterval(500);
			QObject::connect(fullscreenPoll, &QTimer::timeout, VirtualDesktops::instance(), [] {
				stack->setFullscreenAppActive(foregroundAppIsFullscreen());
			});
			fullscreenPoll->start();

			QObject::connect(
			    VirtualDesktops::instance(),
			    &VirtualDesktops::currentChanged,
			    VirtualDesktops::instance(),
			    [] { stack->followCurrentDesktop(); }
			);
		}

		return stack;
	}

	void followCurrentDesktop() {
		auto* desktops = VirtualDesktops::instance();

		for (auto* panel: this->mPanels) {
			if (panel->pinnedToAllDesktops || panel->isEmbedded()) continue;
			auto* hwnd = panel->hwnd();
			if (hwnd == nullptr) continue;
			if (!desktops->isWindowOnCurrent(hwnd)) desktops->moveWindow(hwnd, desktops->currentIndex());
			panel->stuckHwnd = hwnd;
			panel->stuckDesktop = desktops->currentIndex();
		}
	}

	void addPanel(WinPanelWindow* panel) {
		if (!this->mPanels.contains(panel)) this->mPanels.push_back(panel);
	}

	void removePanel(WinPanelWindow* panel) { this->mPanels.removeOne(panel); }

	void restack(HWND hwnd, HWND insertAfter, bool raiseOverlays) {
		constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;

		QList<HWND> ownAbove;
		auto inPlace = false;
		if (insertAfter == HWND_TOPMOST) inPlace = onTopOfOthers(hwnd, ownAbove);
		else if (insertAfter == HWND_BOTTOM) inPlace = onlyBelow(hwnd, &isOwnProcessWindow);

		QList<HWND> overlays;
		if (raiseOverlays && insertAfter == HWND_TOPMOST) {
			for (auto* panel: this->mPanels | std::views::reverse) {
				if (panel->bLayer != PanelLayer::Overlay) continue;
				auto* overlay = panel->hwnd();
				if (overlay == nullptr || overlay == hwnd || !IsWindowVisible(overlay)) continue;
				if (inPlace && ownAbove.contains(overlay)) continue;
				if (overlaps(hwnd, overlay)) overlays.append(overlay);
			}
		}

		if (overlays.isEmpty()) {
			if (!inPlace) SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0, flags);
			return;
		}

		auto* batch = BeginDeferWindowPos(static_cast<int>(overlays.length()) + 1);
		auto* previous = HWND_TOPMOST;

		for (auto* overlay: overlays) {
			if (batch != nullptr) batch = DeferWindowPos(batch, overlay, previous, 0, 0, 0, 0, flags);
			previous = overlay;
		}

		if (batch != nullptr && !inPlace) {
			batch = DeferWindowPos(batch, hwnd, previous, 0, 0, 0, 0, flags);
		}

		if (batch != nullptr && EndDeferWindowPos(batch)) return;

		if (!inPlace) SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0, flags);
		for (auto* overlay: overlays) SetWindowPos(overlay, HWND_TOPMOST, 0, 0, 0, 0, flags);
	}

	void lowerBackgrounds() {
		auto isBackground = [this](HWND hwnd) {
			return std::ranges::any_of(this->mPanels, [hwnd](WinPanelWindow* panel) {
				return panel->bLayer == PanelLayer::Background && panel->hwnd() == hwnd;
			});
		};

		for (auto* panel: this->mPanels) {
			if (panel->bLayer != PanelLayer::Background || panel->isEmbedded()) continue;
			auto* hwnd = panel->hwnd();
			if (hwnd == nullptr || onlyBelow(hwnd, isBackground)) continue;
			SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}

	[[nodiscard]] bool fullscreenAppActive() const { return this->mFullscreenApp; }

	void setFullscreenAppActive(bool active) {
		if (active == this->mFullscreenApp) return;
		this->mFullscreenApp = active;
		WindowManager::setFullscreenApp(active); // QML: game mode follows it

		for (auto* panel: this->mPanels) {
			if (panel->bLayer == PanelLayer::Top) panel->updateLayer();
		}
	}

private:
	QList<WinPanelWindow*> mPanels;
	bool mFullscreenApp = false;
};

bool WinProxiedWindow::event(QEvent* event) {
	if (event->type() == QEvent::Resize && this->mPanel != nullptr) {
		auto handled = this->ProxiedWindow::event(event);
		this->mPanel->boundInputMask();
		return handled;
	}

	if (event->type() == QEvent::PlatformSurface) {
		auto* surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event); // NOLINT

		if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
			emit this->surfaceCreated();
		}
	}

	return this->ProxiedWindow::event(event);
}

void WinProxiedWindow::setPanel(WinPanelWindow* panel) { this->mPanel = panel; }

bool WinProxiedWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
	if (eventType == "windows_generic_MSG" && !this->mPanel.isNull()) {
		if (this->mPanel->handleNativeMessage(static_cast<MSG*>(message), result)) return true;
	}

	return this->ProxiedWindow::nativeEvent(eventType, message, result);
}

WinPanelWindow::WinPanelWindow(QObject* parent)
    : ProxyWindowBase(parent)
    , blur(new PanelBlur(this)) {
	this->bcExclusiveZone.setBinding([this]() -> qint32 {
		switch (this->bExclusionMode.value()) {
		case ExclusionMode::Ignore: return 0;
		case ExclusionMode::Normal: return this->bExclusiveZone;
		case ExclusionMode::Auto:
			auto edge = this->bcExclusionEdge.value();
			auto margins = this->bMargins.value();

			if (edge == Qt::TopEdge || edge == Qt::BottomEdge) {
				return this->bImplicitHeight + margins.top + margins.bottom;
			} else if (edge == Qt::LeftEdge || edge == Qt::RightEdge) {
				return this->bImplicitWidth + margins.left + margins.right;
			} else {
				return 0;
			}
		}

		return 0;
	});

	this->bcExclusionEdge.setBinding([this] { return this->bAnchors.value().exclusionEdge(); });

	auto* host = DesktopHost::instance();
	auto queued = Qt::QueuedConnection;
	QObject::connect(
	    host,
	    &DesktopHost::parentChanged,
	    this,
	    [this] {
		    this->embedRefusedBy = nullptr;
		    this->updateLayer();
	    },
	    queued
	);
	QObject::connect(host, &DesktopHost::parentMoved, this, &WinPanelWindow::placeEmbedded, queued);
	QObject::connect(
	    host,
	    &DesktopHost::aboveIconsChanged,
	    this,
	    &WinPanelWindow::updateLayer,
	    queued
	);
	QObject::connect(host, &DesktopHost::iconsRestacked, this, &WinPanelWindow::keepAboveIcons);
}

WinPanelWindow::~WinPanelWindow() {
	this->releaseNativeState();
}

ProxiedWindow* WinPanelWindow::retrieveWindow(QObject* oldInstance) {
	auto* old = qobject_cast<WinPanelWindow*>(oldInstance);
	if (old == nullptr) return nullptr;

	this->appBar.adopt(old->appBar);
	this->blur->adopt(old->blur);
	this->mEmbedParent = old->mEmbedParent;
	this->mEmbedInsertAfter = old->mEmbedInsertAfter;
	this->mEmbedAboveIcons = old->mEmbedAboveIcons;
	this->mEmbedRect = old->mEmbedRect;
	this->mInputMask = old->mInputMask;
	this->mHasInputMask = old->mHasInputMask;
	this->mRequestedMask = old->mRequestedMask;
	this->mAppliedRegion = old->mAppliedRegion;
	this->mRegionApplied = old->mRegionApplied;

	return old->disownWindow();
}

ProxiedWindow* WinPanelWindow::createQQuickWindow() { return new WinProxiedWindow(this); }

void WinPanelWindow::connectWindow() {
	this->ProxyWindowBase::connectWindow();

	if (auto* window = qobject_cast<WinProxiedWindow*>(this->window)) {
		window->setPanel(this);

		QObject::connect(
		    window,
		    &WinProxiedWindow::surfaceCreated,
		    this,
		    &WinPanelWindow::onSurfaceCreated
		);
	} else {
		qCWarning(logPanel) << "Backing window of" << this
		                    << "is not a WinProxiedWindow, native messages will be lost.";
	}

	// clang-format off
	QObject::connect(this->window, &QQuickWindow::visibleChanged, this, &WinPanelWindow::onWindowVisibleChanged);
	QObject::connect(this->window, &ProxiedWindow::exposed, this, &WinPanelWindow::onWindowExposed);
	QObject::connect(this->window, &QWindow::screenChanged, this, &WinPanelWindow::onWindowScreenChanged);
	QObject::connect(this->window, &QWindow::screenChanged, this, &WinPanelWindow::updateScreen);
	QObject::connect(this->window, &ProxiedWindow::devicePixelRatioChanged, this, &WinPanelWindow::scheduleUpdateDimensions);
	// clang-format on

	QObject::connect(
	    VirtualDesktops::instance(),
	    &VirtualDesktops::accessorReady,
	    this,
	    &WinPanelWindow::onDesktopAccessorReady,
	    Qt::UniqueConnection
	);

	this->updateScreen();

	if (this->hwnd() == nullptr) {
		this->mEmbedParent = nullptr;
		this->mEmbedInsertAfter = nullptr;
		this->mEmbedAboveIcons = false;
		this->mRegionApplied = false;
		this->mAppliedRegion = QRegion();
	}

	Qt::WindowFlags flags = Qt::Tool | Qt::FramelessWindowHint;
	if (this->bLayer.value() >= PanelLayer::Top) flags |= Qt::WindowStaysOnTopHint;
	if (this->bKeyboardFocus.value() == PanelKeyboardFocus::None) {
		flags |= Qt::WindowDoesNotAcceptFocus;
	}

	this->window->setFlags(flags);
	if (this->hwnd() != nullptr) this->applyNativeStyles();
	this->updateLayer();
	this->updateFocus();

	if (this->window->handle() != nullptr) {
		this->nativeInit();
	} else {
		this->updateDimensions();
	}
}

ProxiedWindow* WinPanelWindow::disownWindow(bool keepItemOwnership) {
	this->releaseNativeState();
	return this->ProxyWindowBase::disownWindow(keepItemOwnership);
}

void WinPanelWindow::releaseNativeState() {
	if (this->mClosing && this->window != nullptr && this->window->isVisible()) {
		this->quietVisibilityChange = true;
		this->window->setVisible(false);
		this->quietVisibilityChange = false;
	}

	this->mClosing = false;
	this->resetAnimation();

	if (this->window != nullptr) {
		InputMaskTracker::instance()->remove(this->window);

		if (auto* window = qobject_cast<WinProxiedWindow*>(this->window)) {
			window->setPanel(nullptr);
		}
	}

	this->appBar.remove();
	this->blur->release();
	WinPanelStack::instance()->removePanel(this);

	this->mEmbedParent = nullptr;
	this->mEmbedInsertAfter = nullptr;
	this->mEmbedAboveIcons = false;
	this->mRegionApplied = false;
	this->mButtonHeld = false;
	this->destroyedHwnd = nullptr;
	this->maskRouted = false;
	this->focusOnExpose = false;
}

void WinPanelWindow::trySetWidth(qint32 implicitWidth) {
	if (!this->bAnchors.value().horizontalConstraint()) {
		if (this->hwnd() == nullptr || this->mTrackedScreen == nullptr) {
			this->ProxyWindowBase::trySetWidth(implicitWidth);
		}

		this->updateDimensions();
	}
}

void WinPanelWindow::trySetHeight(qint32 implicitHeight) {
	if (!this->bAnchors.value().verticalConstraint()) {
		if (this->hwnd() == nullptr || this->mTrackedScreen == nullptr) {
			this->ProxyWindowBase::trySetHeight(implicitHeight);
		}

		this->updateDimensions();
	}
}

void WinPanelWindow::onReload(QObject* oldInstance) {
	auto instant = std::exchange(this->mInstantVisibility, oldInstance != nullptr);
	this->ProxyWindowBase::onReload(oldInstance);
	this->mInstantVisibility = instant;
}

void WinPanelWindow::setScreen(QuickshellScreenInfo* screen) {
	auto instant = std::exchange(this->mInstantVisibility, true);
	this->ProxyWindowBase::setScreen(screen);
	this->mInstantVisibility = instant;
	this->updateScreen();
}

bool WinPanelWindow::aboveWindows() const { return this->bLayer.value() > PanelLayer::Bottom; }

bool WinPanelWindow::isVisibleDirect() const {
	return !this->mClosing && this->ProxyWindowBase::isVisibleDirect();
}

void WinPanelWindow::setVisibleDirect(bool visible) {
	if (!visible) {
		if (this->mClosing) return;

		std::optional<PanelAnimationChoice> choice;
		if (this->isVisibleDirect() && this->hwnd() != nullptr && this->window->isExposed()) {
			choice = this->animationChoice();
		}

		if (choice.has_value()) {
			this->beginClose(*choice);
			return;
		}

		this->resetAnimation();
		this->ProxyWindowBase::setVisibleDirect(false);
		return;
	}

	if (this->mClosing) {
		this->reopen();
		return;
	}

	auto wasShown = this->isVisibleDirect();

	if (this->mScreen == nullptr && !wasShown) {
		if (this->window == nullptr) this->createWindow();
		this->followFocusedScreen();
	}

	std::optional<PanelAnimationChoice> choice;
	if (!wasShown) choice = this->animationChoice();

	if (choice.has_value()) {
		if (this->window == nullptr) this->createWindow();
		this->prepareShowAnimation(*choice);
	}

	this->ProxyWindowBase::setVisibleDirect(true);

	if (choice.has_value()) {
		if (this->isVisibleDirect()) this->runAnimation(true);
		else this->resetAnimation();
	}
}

void WinPanelWindow::followFocusedScreen() {
	if (this->window == nullptr || this->mEmbedParent != nullptr) return;

	QScreen* screen = nullptr;
	POINT cursor {};
	if (GetCursorPos(&cursor)) {
		screen = screenForMonitor(MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL));
	}
	if (screen == nullptr) screen = focusedScreen();
	if (screen == nullptr) screen = QGuiApplication::screenAt(QCursor::pos());
	if (screen == nullptr || this->window->screen() == screen) return;

	this->window->setScreen(screen);
	this->updateScreen();
	this->updateDimensions();
}

void WinPanelWindow::setAboveWindows(bool aboveWindows) {
	this->setLayer(aboveWindows ? PanelLayer::Top : PanelLayer::Bottom);
}

bool WinPanelWindow::focusable() const {
	return this->bKeyboardFocus.value() != PanelKeyboardFocus::None;
}

void WinPanelWindow::setFocusable(bool focusable) {
	this->setKeyboardFocus(focusable ? PanelKeyboardFocus::OnDemand : PanelKeyboardFocus::None);
}

HWND WinPanelWindow::hwnd() const { return hwndOf(this->window); }

void WinPanelWindow::onSurfaceCreated() { this->nativeInit(); }

void WinPanelWindow::nativeInit() {
	if (this->hwnd() == nullptr) return;

	if (this->window->isVisible()) {
		WinPanelStack::instance()->addPanel(this);
		this->stickToAllDesktops();
	}

	applyPanelDwmAttributes(this->hwnd());
	this->applyNativeStyles();
	this->updateDimensions();
	this->updateLayer();
	this->updateFocus();
	this->blur->attach();
}

void WinPanelWindow::applyNativeStyles() {
	auto* hwnd = this->hwnd();
	setExStyleBits(hwnd, WS_EX_TOOLWINDOW, true);

	if (this->mEmbedParent != nullptr) {
		setChildStyle(hwnd, true);
		setExStyleBits(hwnd, WS_EX_NOPARENTNOTIFY, true);

		if (forceLayeredDesktopPanels()) {
			setExStyleBits(hwnd, WS_EX_LAYERED, true);
			SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
		}
	}
}

void WinPanelWindow::stickToAllDesktops() {
	auto* hwnd = this->hwnd();
	if (hwnd == nullptr || this->pinnedToAllDesktops || this->mEmbedParent != nullptr) return;

	auto* desktops = VirtualDesktops::instance();
	auto current = desktops->currentIndex();

	if (this->stuckHwnd == hwnd && this->stuckDesktop != current
	    && !desktops->isWindowOnCurrent(hwnd))
	{
		desktops->moveWindow(hwnd, current);
	}

	this->stuckHwnd = hwnd;
	this->stuckDesktop = current;

	QTimer::singleShot(0, this, [this] {
		auto* hwnd = this->hwnd();
		if (hwnd == nullptr || !this->window->isVisible() || this->pinnedToAllDesktops
		    || this->mEmbedParent != nullptr)
			return;

		auto* desktops = VirtualDesktops::instance();
		if (!desktops->accessorLoaded() || this->pinFailedHwnd == hwnd) return;

		this->pinnedToAllDesktops = desktops->isWindowPinned(hwnd) || desktops->pinWindow(hwnd, true);

		if (!this->pinnedToAllDesktops) {
			this->pinFailedHwnd = hwnd;
			qCDebug(logPanel) << "Could not pin" << this << "to all desktops; it will follow the current one";
		}
	});
}

void WinPanelWindow::onDesktopAccessorReady() {
	if (this->window != nullptr && this->window->isVisible()) this->stickToAllDesktops();
}

void WinPanelWindow::onWindowVisibleChanged() {
	if (this->window->isVisible()) {
		startup::watchWindow(this->window);
		WinPanelStack::instance()->addPanel(this);
		this->updateFocusFlag();
		this->updateDimensions();
		this->updateLayer();
		this->stickToAllDesktops();
		this->focusWhenExposed();
	} else {
		this->focusOnExpose = false;
		this->appBar.remove();
		WinPanelStack::instance()->removePanel(this);
	}

	this->blur->syncPlacement();
	InputMaskTracker::instance()->refresh();
}

void WinPanelWindow::updateScreen() {
	auto* newScreen =
	    this->mScreen ? this->mScreen : (this->window ? this->window->screen() : nullptr);

	if (newScreen == this->mTrackedScreen) return;

	if (this->mTrackedScreen != nullptr) {
		QObject::disconnect(this->mTrackedScreen, nullptr, this, nullptr);
	}

	this->mTrackedScreen = newScreen;

	if (this->mTrackedScreen != nullptr) {
		// clang-format off
		QObject::connect(this->mTrackedScreen, &QScreen::geometryChanged, this, &WinPanelWindow::onScreenScaleChanged);
		QObject::connect(this->mTrackedScreen, &QScreen::availableGeometryChanged, this, &WinPanelWindow::scheduleUpdateDimensions);
		QObject::connect(this->mTrackedScreen, &QScreen::logicalDotsPerInchChanged, this, &WinPanelWindow::onScreenScaleChanged);
		QObject::connect(this->mTrackedScreen, &QScreen::physicalDotsPerInchChanged, this, &WinPanelWindow::onScreenScaleChanged);
		// clang-format on
	}

	this->updateDimensions();
}

void WinPanelWindow::onScreenScaleChanged() {
	if (this->mEmbedParent != nullptr && this->window != nullptr && this->mTrackedScreen != nullptr
	    && !this->scaleRecreatePending
	    && !qFuzzyCompare(this->window->devicePixelRatio(), this->mTrackedScreen->devicePixelRatio()))
	{
		this->scaleRecreatePending = true;
		QTimer::singleShot(300, this, [this]() {
			this->scaleRecreatePending = false;
			if (this->window == nullptr || this->mEmbedParent == nullptr) return;

			qCInfo(logPanel) << "Screen scale changed under" << this << "inside the desktop, recreating its window";
			auto visible = this->window->isVisible();
			this->deleteWindow();
			this->createWindow();
			if (visible) this->setVisible(true);
		});
	}

	this->scheduleUpdateDimensions();
}

void WinPanelWindow::scheduleUpdateDimensions() {
	if (this->dimensionsUpdatePending) return;
	this->dimensionsUpdatePending = true;

	QTimer::singleShot(0, this, [this]() {
		this->dimensionsUpdatePending = false;
		this->updateDimensions();
	});
}

void WinPanelWindow::updateDimensions() {
	if (this->window == nullptr || this->mTrackedScreen == nullptr) return;

	auto* screen = this->mTrackedScreen.data();
	auto screenGeometry = screen->geometry();
	auto rects = monitorRects(monitorForScreen(screen));
	auto mapper = ScreenMapper(screenGeometry, rects.monitor, screen->devicePixelRatio());
	auto* hwnd = this->hwnd();

	auto ignoreZones = this->bExclusionMode == ExclusionMode::Ignore
	                || (this->bExclusionMode == ExclusionMode::Normal && this->bExclusiveZone < 0);

	QRect base;
	if (ignoreZones) base = screenGeometry;
	else if (rects.valid) base = mapper.toLogical(rects.work);
	else base = screen->availableGeometry();

	auto edge = this->bcExclusionEdge.value();
	auto zone = this->bcExclusiveZone.value();
	auto wantsAppBar = !ignoreZones && edge != 0 && zone > 0 && hwnd != nullptr && rects.valid
	                && this->isVisibleDirect();

	if (wantsAppBar && !startup::settled()) {
		wantsAppBar = false;

		if (!this->appBarDeferred) {
			this->appBarDeferred = true;
			startup::afterFirstFrame(this, [this]() {
				this->appBarDeferred = false;
				this->scheduleUpdateDimensions();
			});
		}
	}

	if (wantsAppBar) {
		UINT abEdge = ABE_TOP;
		switch (edge) {
		case Qt::LeftEdge: abEdge = ABE_LEFT; break;
		case Qt::RightEdge: abEdge = ABE_RIGHT; break;
		case Qt::TopEdge: abEdge = ABE_TOP; break;
		case Qt::BottomEdge: abEdge = ABE_BOTTOM; break;
		default: break;
		}

		auto reserved = this->appBar.reserve(hwnd, abEdge, rects.monitor, mapper.toPhysical(zone));

		if (reserved.isValid()) {
			auto reservedLogical = mapper.toLogical(reserved);

			// Top/bottom bars span the whole monitor width like layer-shell does on Linux: a side dock's
			// reservation must not shorten them (the DMS island bar started 61 px in, next to the dock).
			auto monitorLogical = mapper.toLogical(rects.monitor);
			switch (edge) {
			case Qt::TopEdge:
				base.setTop(std::min(base.top(), reservedLogical.top()));
				base.setLeft(monitorLogical.left());
				base.setRight(monitorLogical.right());
				break;
			case Qt::BottomEdge:
				base.setBottom(std::max(base.bottom(), reservedLogical.bottom()));
				base.setLeft(monitorLogical.left());
				base.setRight(monitorLogical.right());
				break;
			case Qt::LeftEdge: base.setLeft(std::min(base.left(), reservedLogical.left())); break;
			case Qt::RightEdge: base.setRight(std::max(base.right(), reservedLogical.right())); break;
			default: break;
			}
		}
	} else {
		this->appBar.remove();
	}

	if (this->mClosing) return;

	auto geometry = QRect();

	auto anchors = this->bAnchors.value();
	auto margins = this->bMargins.value();

	if (anchors.horizontalConstraint()) {
		geometry.setX(base.x() + margins.left);
		geometry.setWidth(base.width() - margins.left - margins.right);
	} else {
		if (anchors.mLeft) {
			geometry.setX(base.x() + margins.left);
		} else if (anchors.mRight) {
			geometry.setX(base.x() + base.width() - this->implicitWidth() - margins.right);
		} else {
			geometry.setX(base.x() + base.width() / 2 - this->implicitWidth() / 2);
		}

		geometry.setWidth(this->implicitWidth());
	}

	if (anchors.verticalConstraint()) {
		geometry.setY(base.y() + margins.top);
		geometry.setHeight(base.height() - margins.top - margins.bottom);
	} else {
		if (anchors.mTop) {
			geometry.setY(base.y() + margins.top);
		} else if (anchors.mBottom) {
			geometry.setY(base.y() + base.height() - this->implicitHeight() - margins.bottom);
		} else {
			geometry.setY(base.y() + base.height() / 2 - this->implicitHeight() / 2);
		}

		geometry.setHeight(this->implicitHeight());
	}

	if (this->mEmbedParent != nullptr) this->mEmbedRect = mapper.toPhysical(geometry);

	if (this->window->geometry() != geometry) {
		qCDebug(logPanel) << "Placing" << this << "at" << geometry << "on" << screen->name();
		this->window->setGeometry(geometry);
	}

	if (this->mEmbedParent != nullptr) {
		this->placeEmbedded();
		if (!this->wantsEmbedding()) this->updateLayer();
	}
}

void WinPanelWindow::updateLayer() {
	if (this->window == nullptr) return;

	auto layer = this->bLayer.value();
	auto* hwnd = this->hwnd();

	if (hwnd != nullptr) this->updateEmbedding();

	auto onTop = layer >= PanelLayer::Top;
	if (this->window->flags().testFlag(Qt::WindowStaysOnTopHint) != onTop) {
		this->window->setFlag(Qt::WindowStaysOnTopHint, onTop);
	}

	if (hwnd == nullptr) return;

	this->applyNativeStyles();

	if (this->mEmbedParent != nullptr) {
		InputMaskTracker::instance()->refresh();
		return;
	}

	HWND insertAfter = nullptr;
	switch (layer) {
	case PanelLayer::Background:
	case PanelLayer::Bottom: insertAfter = HWND_BOTTOM; break;
	case PanelLayer::Top:
		insertAfter = WinPanelStack::instance()->fullscreenAppActive() ? HWND_BOTTOM : HWND_TOPMOST;
		break;
	case PanelLayer::Overlay: insertAfter = HWND_TOPMOST; break;
	}

	auto* stack = WinPanelStack::instance();
	// back from Top: clear WS_EX_TOPMOST first (HWND_BOTTOM alone does not take a window out of the topmost band)
	if (insertAfter == HWND_BOTTOM && (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) {
		SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	stack->restack(hwnd, insertAfter, layer != PanelLayer::Overlay);
	if (layer == PanelLayer::Bottom) stack->lowerBackgrounds();

	InputMaskTracker::instance()->refresh();
}

bool WinPanelWindow::reservesSpace() const {
	auto mode = this->bExclusionMode.value();
	if (mode == ExclusionMode::Ignore) return false;
	if (mode == ExclusionMode::Normal && this->bExclusiveZone.value() < 0) return false;
	return this->bcExclusionEdge.value() != 0 && this->bcExclusiveZone.value() > 0;
}

bool WinPanelWindow::wantsEmbedding() const {
	return DesktopHost::instance()->enabled() && this->bLayer.value() <= PanelLayer::Bottom
	    && this->bKeyboardFocus.value() == PanelKeyboardFocus::None && !this->reservesSpace();
}

bool WinPanelWindow::wantsAboveIcons() const {
	return DesktopHost::instance()->aboveIcons().contains(this->bNamespace.value());
}

void WinPanelWindow::updateEmbedding() {
	auto* hwnd = this->hwnd();
	if (hwnd == nullptr) return;

	if (this->mEmbedParent != nullptr && GetAncestor(hwnd, GA_PARENT) != this->mEmbedParent) {
		this->mEmbedParent = nullptr;
		this->mEmbedInsertAfter = nullptr;
		this->mEmbedAboveIcons = false;
	}

	auto* host = DesktopHost::instance();
	auto embed = this->wantsEmbedding();
	auto aboveIcons = embed && this->wantsAboveIcons();

	HWND parent = nullptr;
	if (embed) parent = aboveIcons ? host->iconsHost() : host->parentWindow();
	if (parent != nullptr && parent == this->embedRefusedBy) parent = nullptr;
	if (parent == nullptr) aboveIcons = false;
	auto* insertAfter = parent == nullptr || aboveIcons ? nullptr : host->insertAfter();

	if (parent == this->mEmbedParent && insertAfter == this->mEmbedInsertAfter
	    && aboveIcons == this->mEmbedAboveIcons)
	{
		return;
	}

	if (parent != nullptr) this->embedInto(parent, insertAfter, aboveIcons);
	else this->unembed();
}

void WinPanelWindow::embedInto(HWND parent, HWND insertAfter, bool aboveIcons) {
	auto* hwnd = this->hwnd();
	auto wasEmbedded = this->mEmbedParent != nullptr;

	if (!wasEmbedded) {
		RECT rect {};
		if (GetWindowRect(hwnd, &rect)) this->mEmbedRect = toQRect(rect);
		setChildStyle(hwnd, true);
	}

	this->mEmbedParent = parent;
	this->mEmbedInsertAfter = insertAfter;
	this->mEmbedAboveIcons = aboveIcons;

	SetLastError(0);
	if (SetParent(hwnd, parent) == nullptr && GetLastError() != 0) {
		qCWarning(logPanel) << "Could not put" << this << "into the desktop, error" << GetLastError()
		                    << "- it stays a window";
		this->embedRefusedBy = parent;
		if (wasEmbedded) this->unembed();
		else {
			this->mEmbedParent = nullptr;
			this->mEmbedInsertAfter = nullptr;
			this->mEmbedAboveIcons = false;
			setChildStyle(hwnd, false);
		}
		return;
	}

	qCDebug(logPanel) << "Put" << this << "into the desktop window" << parent
	                  << (aboveIcons ? "above the icons" : "behind the icons");

	this->embedRefusedBy = nullptr;
	this->restackCount = 0;
	this->pinnedToAllDesktops = false;
	this->appBar.remove();
	this->applyNativeStyles();
	this->placeEmbedded();
	this->routeInputMask();
	this->blur->attach();
}

void WinPanelWindow::unembed() {
	auto* hwnd = this->hwnd();
	auto wasEmbedded = this->mEmbedParent != nullptr;
	this->mEmbedParent = nullptr;
	this->mEmbedInsertAfter = nullptr;
	this->mEmbedAboveIcons = false;
	this->mButtonHeld = false;
	if (hwnd == nullptr || !wasEmbedded) return;

	SetParent(hwnd, nullptr);
	setChildStyle(hwnd, false);
	setExStyleBits(hwnd, WS_EX_NOPARENTNOTIFY, false);
	if (forceLayeredDesktopPanels()) setExStyleBits(hwnd, WS_EX_LAYERED, false);
	this->routeInputMask();

	UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED;
	auto rect = this->mEmbedRect;
	if (!rect.isValid()) flags |= SWP_NOMOVE | SWP_NOSIZE;
	SetWindowPos(hwnd, nullptr, rect.x(), rect.y(), rect.width(), rect.height(), flags);

	qCDebug(logPanel) << "Took" << this << "out of the desktop";

	this->blur->attach();
	if (this->window->isVisible()) this->stickToAllDesktops();
}

QRect WinPanelWindow::embeddedRect() const {
	auto rect = toRECT(this->mEmbedRect);
	MapWindowPoints(HWND_DESKTOP, this->mEmbedParent, reinterpret_cast<POINT*>(&rect), 2); // NOLINT
	return toQRect(rect);
}

void WinPanelWindow::placeEmbedded() {
	auto* hwnd = this->hwnd();
	if (hwnd == nullptr || this->mEmbedParent == nullptr || !this->mEmbedRect.isValid()) return;

	auto rect = this->embeddedRect();
	UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
	auto* insertAfter = this->mEmbedAboveIcons ? HWND_TOP : this->mEmbedInsertAfter;
	if (insertAfter == nullptr && !this->mEmbedAboveIcons) flags |= SWP_NOZORDER;

	this->placingEmbedded = true;
	SetWindowPos(
	    hwnd,
	    insertAfter,
	    rect.x(),
	    rect.y(),
	    rect.width(),
	    rect.height(),
	    flags
	);
	this->placingEmbedded = false;

	if (this->mEmbedAboveIcons) this->applyInputRegion();
}

void WinPanelWindow::keepAboveIcons() {
	auto* hwnd = this->hwnd();
	if (hwnd == nullptr || !this->mEmbedAboveIcons) return;

	auto covered = false;
	for (auto* above = GetWindow(hwnd, GW_HWNDPREV); above != nullptr;
	     above = GetWindow(above, GW_HWNDPREV))
	{
		if (IsWindowVisible(above) && !isOwnProcessWindow(above)) {
			covered = true;
			break;
		}
	}

	if (!covered) return;

	auto now = GetTickCount();
	if (this->restackCount == 0 || now - this->restackWindowStart > RESTACK_WINDOW_MS) {
		this->restackCount = 0;
		this->restackWindowStart = now;
	}

	if (++this->restackCount > MAX_RESTACKS) {
		if (this->restackCount == MAX_RESTACKS + 1) {
			qCWarning(logPanel) << "Explorer keeps covering" << this
			                    << "with the desktop icons; leaving it there";
		}

		return;
	}

	qCDebug(logPanel) << "Putting" << this << "back above the desktop icons";
	this->placeEmbedded();
}

void WinPanelWindow::routeInputMask() {
	if (this->window == nullptr) return;
	auto* tracker = InputMaskTracker::instance();

	if (this->mClosing) {
		tracker->setMask(this->window, QRegion());
	} else if (this->mHasInputMask && !this->mEmbedAboveIcons) {
		tracker->setMask(this->window, this->mInputMask);
	} else {
		tracker->remove(this->window);
	}

	this->applyInputRegion();
}

void WinPanelWindow::applyInputRegion() {
	auto* hwnd = this->hwnd();
	if (hwnd == nullptr || this->window == nullptr) return;

	auto wantsRegion = this->mEmbedAboveIcons && this->mHasInputMask && !this->mButtonHeld;
	auto region = wantsRegion ? toPhysicalRegion(this->mInputMask, this->window->devicePixelRatio())
	                          : QRegion();

	if (wantsRegion == this->mRegionApplied && region == this->mAppliedRegion) return;

	auto* hrgn = wantsRegion ? toHrgn(region) : nullptr;

	if (SetWindowRgn(hwnd, hrgn, TRUE) == 0) {
		if (hrgn != nullptr) DeleteObject(hrgn);
		qCWarning(logPanel) << "Could not set the window region of" << this;
		return;
	}

	this->mRegionApplied = wantsRegion;
	this->mAppliedRegion = region;
}

void WinPanelWindow::setButtonHeld(bool held) {
	if (held == this->mButtonHeld) return;
	this->mButtonHeld = held;
	this->applyInputRegion();
}

void WinPanelWindow::onWindowScreenChanged() {
	if (this->mEmbedParent == nullptr || this->mTrackedScreen == nullptr || this->screenRestorePending)
	{
		return;
	}

	if (this->window->screen() == this->mTrackedScreen) return;
	this->screenRestorePending = true;

	QTimer::singleShot(0, this, [this] {
		this->screenRestorePending = false;

		if (this->window != nullptr && this->mEmbedParent != nullptr && this->mTrackedScreen != nullptr
		    && this->window->screen() != this->mTrackedScreen)
		{
			this->window->setScreen(this->mTrackedScreen);
		}
	});
}

void WinPanelWindow::recreateDestroyedWindow() {
	auto* dead = std::exchange(this->destroyedHwnd, nullptr);
	if (dead == nullptr || this->window == nullptr || IsWindow(dead)) return;

	qCInfo(logPanel) << "The desktop went away with" << this << "inside, recreating its window";

	auto visible = this->visibleWhenDestroyed;
	this->deleteWindow();
	this->createWindow();
	if (visible) this->setVisible(true);
}

void WinPanelWindow::updateFocus() {
	if (this->window == nullptr) return;

	auto focus = this->bKeyboardFocus.value();

	if (this->hwnd() != nullptr && this->wantsEmbedding() != (this->mEmbedParent != nullptr)) {
		this->updateLayer();
	}

	this->updateFocusFlag();

	if (focus == PanelKeyboardFocus::Exclusive && this->isVisibleDirect()) {
		this->focusWhenExposed();
	}

	InputMaskTracker::instance()->refresh();
}

void WinPanelWindow::updateFocusFlag() {
	auto noFocus = this->bKeyboardFocus.value() == PanelKeyboardFocus::None;
	if (this->window->flags().testFlag(Qt::WindowDoesNotAcceptFocus) == noFocus) return;

	if (!noFocus || this->window->handle() == nullptr) {
		this->applyFocusFlag();
		return;
	}

	if (this->focusFlagPending || !this->isVisibleDirect()) return;
	this->focusFlagPending = true;

	QTimer::singleShot(0, this, [this]() {
		this->focusFlagPending = false;
		if (this->window != nullptr && this->isVisibleDirect()) this->applyFocusFlag();
	});
}

void WinPanelWindow::applyFocusFlag() {
	auto noFocus = this->bKeyboardFocus.value() == PanelKeyboardFocus::None;
	if (this->window->flags().testFlag(Qt::WindowDoesNotAcceptFocus) == noFocus) return;

	this->window->setFlag(Qt::WindowDoesNotAcceptFocus, noFocus);
	if (this->hwnd() != nullptr) this->applyNativeStyles();
	InputMaskTracker::instance()->refresh();
}

void WinPanelWindow::focusWhenExposed() {
	if (this->focusOnExpose) return;
	this->focusOnExpose = true;

	auto request = ++this->focusRequest;
	auto delay = this->window->isExposed() ? 0 : FOCUS_EXPOSE_FALLBACK_MS;

	QTimer::singleShot(delay, this, [this, request]() {
		if (this->focusOnExpose && request == this->focusRequest) this->takeFocusOnExpose();
	});
}

void WinPanelWindow::onWindowExposed() {
	if (this->focusOnExpose && this->window->isExposed()) this->takeFocusOnExpose();
}

void WinPanelWindow::takeFocusOnExpose() {
	this->focusOnExpose = false;

	if (this->window == nullptr || !this->isVisibleDirect()
	    || this->bKeyboardFocus == PanelKeyboardFocus::None)
		return;

	qCDebug(logPanel) << "Taking keyboard focus for" << this << "as it is shown";
	this->grabKeyboardFocus();
}

void WinPanelWindow::grabKeyboardFocus() {
	auto* hwnd = this->hwnd();
	if (hwnd == nullptr) return;

	this->window->requestActivate();
	if (GetForegroundWindow() != hwnd && !forceForegroundWindow(hwnd)) {
		qCWarning(logPanel) << "Could not take exclusive keyboard focus for" << this;
	}
}

void WinPanelWindow::scheduleFocusGrab() {
	if (this->focusGrabPending) return;
	this->focusGrabPending = true;

	QTimer::singleShot(50, this, [this]() {
		this->focusGrabPending = false;

		if (this->window == nullptr || !this->isVisibleDirect()
		    || this->bKeyboardFocus != PanelKeyboardFocus::Exclusive)
			return;

		auto* foreground = GetForegroundWindow();
		if (foreground == this->hwnd() || isOwnProcessWindow(foreground)) return;

		this->grabKeyboardFocus();
	});
}

void WinPanelWindow::applyInputMask(const QRegion& region, bool hasMask) {
	if (this->window == nullptr) return;

	this->mRequestedMask = region;
	this->applyBoundedMask(hasMask);
}

void WinPanelWindow::onPolished() {
	this->setAnimationTransformsBypassed(true);
	this->ProxyWindowBase::onPolished();
	this->setAnimationTransformsBypassed(false);
}

void WinPanelWindow::boundInputMask() {
	if (this->window == nullptr || !this->maskRouted || !this->mHasInputMask) return;
	this->applyBoundedMask(true);
}

void WinPanelWindow::applyBoundedMask(bool hasMask) {
	auto region = hasMask ? this->mRequestedMask.intersected(QRect(QPoint(0, 0), this->window->size()))
	                      : this->mRequestedMask;

	if (this->maskRouted && hasMask == this->mHasInputMask && region == this->mInputMask) return;

	this->mInputMask = region;
	this->mHasInputMask = hasMask;
	this->routeInputMask();
	this->maskRouted = true;

	this->blur->setInputMask(region, hasMask);
}

bool WinPanelWindow::handleNativeMessage(MSG* msg, qintptr* result) {
	if (this->window == nullptr) return false;

	if (this->mEmbedParent != nullptr) {
		switch (msg->message) {
		case WM_WINDOWPOSCHANGING: {
			auto* pos = reinterpret_cast<WINDOWPOS*>(msg->lParam); // NOLINT(performance-no-int-to-ptr)

			if (this->mEmbedRect.isValid()) {
				auto rect = this->embeddedRect();

				if (!(pos->flags & SWP_NOMOVE)) {
					pos->x = rect.x();
					pos->y = rect.y();
				}

				if (!(pos->flags & SWP_NOSIZE)) {
					pos->cx = rect.width();
					pos->cy = rect.height();
				}
			}

			if (!this->placingEmbedded) pos->flags |= SWP_NOZORDER;

			*result = 0;
			return true;
		}
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONDBLCLK:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONDBLCLK:
			if (this->mEmbedAboveIcons) this->setButtonHeld(true);
			break;
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
		case WM_XBUTTONUP:
			if (GetCapture() != msg->hwnd) this->setButtonHeld(false);
			break;
		case WM_CAPTURECHANGED:
			if (reinterpret_cast<HWND>(msg->lParam) != msg->hwnd) this->setButtonHeld(false); // NOLINT
			break;
		case WM_DESTROY:
			this->mEmbedParent = nullptr;
			this->mEmbedInsertAfter = nullptr;
			this->mEmbedAboveIcons = false;
			this->mRegionApplied = false;
			this->mAppliedRegion = QRegion();
			this->mButtonHeld = false;
			this->destroyedHwnd = msg->hwnd;
			this->visibleWhenDestroyed = this->window->isVisible();
			QTimer::singleShot(0, this, &WinPanelWindow::recreateDestroyedWindow);
			return false;
		default: break;
		}
	}

	if (msg->message == WinAppBar::callbackMessage()) {
		switch (msg->wParam) {
		case ABN_STATECHANGE:
		case ABN_POSCHANGED:
			this->appBar.invalidatePosition();
			this->scheduleUpdateDimensions();
			break;
		case ABN_FULLSCREENAPP: {
			auto active = msg->lParam != 0;
			if (active && isOwnProcessWindow(GetForegroundWindow())) active = false;
			WinPanelStack::instance()->setFullscreenAppActive(active);
			break;
		}
		default: break;
		}

		*result = 0;
		return true;
	}

	if (msg->message == WinAppBar::taskbarCreatedMessage()) {
		this->appBar.invalidate();
		this->scheduleUpdateDimensions();
		this->blur->syncPlacement();
		return false;
	}

	switch (msg->message) {
	case WM_ACTIVATE:
		this->appBar.notifyActivate();

		if (LOWORD(msg->wParam) == WA_INACTIVE
		    && this->bKeyboardFocus == PanelKeyboardFocus::Exclusive)
		{
			this->scheduleFocusGrab();
		}
		break;
	case WM_WINDOWPOSCHANGED: {
		auto* pos = reinterpret_cast<WINDOWPOS*>(msg->lParam); // NOLINT(performance-no-int-to-ptr)

		if (!positionUnchanged(pos)) {
			this->appBar.notifyWindowPosChanged();
			InputMaskTracker::instance()->refresh();
		}

		this->blur->syncPlacement();
		break;
	}
	case WM_DISPLAYCHANGE:
	case WM_DPICHANGED: this->appBar.invalidatePosition(); break;
	case WM_SETTINGCHANGE:
	case WM_THEMECHANGED:
	case WM_SYSCOLORCHANGE:
		if (auto* manager = BlurManager::instance()) manager->scheduleSystemCheck();
		break;
	case WM_POWERBROADCAST:
		if (msg->wParam == PBT_APMPOWERSTATUSCHANGE) {
			if (auto* manager = BlurManager::instance()) manager->scheduleSystemCheck();
		}
		break;
	case WM_DWMCOMPOSITIONCHANGED:
		if (auto* manager = BlurManager::instance()) manager->scheduleSystemCheck(true);
		break;
	case WM_WINDOWPOSCHANGING:
		if (this->bLayer == PanelLayer::Background || this->bLayer == PanelLayer::Bottom) {
			auto* pos = reinterpret_cast<WINDOWPOS*>(msg->lParam); // NOLINT(performance-no-int-to-ptr)
			// HWND_NOTOPMOST only ever lowers: it must pass, or a surface that was lifted to Top (Ryoku's desktop
			// while editing / typing) kept WS_EX_TOPMOST for good after going back to Bottom - the wallpaper over
			// every app, no clicks reaching them (2026-11-15)
			// Windows hands HWND_NOTOPMOST in here as a concrete "insert after" window, so while the surface is still
			// in the topmost band every z-order change passes: the guard only keeps a bottom surface from rising.
			const bool stillTopmost = (GetWindowLongPtrW(msg->hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
			if (!stillTopmost && !(pos->flags & SWP_NOZORDER) && pos->hwndInsertAfter != HWND_BOTTOM
			    && pos->hwndInsertAfter != HWND_NOTOPMOST) {
				if (this->bLayer == PanelLayer::Background) pos->hwndInsertAfter = HWND_BOTTOM;
				else pos->flags |= SWP_NOZORDER;
			}
		}
		break;
	default: break;
	}

	return false;
}

bool WinPanelWindow::animationsAllowed() const {
	if (this->mInstantVisibility || !startup::settled()) return false;
	if (this->mEmbedParent != nullptr || this->wantsEmbedding()) return false;
	if (this->mColor.alpha() != 0) return false;

	auto* manager = BlurManager::instance();
	if (manager == nullptr || manager->shutDown() || !manager->panelAnimations()) return false;

	return systemAnimationsEnabled();
}

std::optional<PanelAnimationChoice> WinPanelWindow::animationChoice() const {
	if (!this->animationsAllowed()) return std::nullopt;

	auto* manager = BlurManager::instance();
	if (auto* generation = EngineGeneration::findObjectGeneration(this)) {
		manager->setShellDir(generation->rootPath.path());
	}

	auto rule = manager->ruleFor(this->bNamespace.value());
	if (!rule.animation.animates()) return std::nullopt;

	auto choice = PanelAnimationChoice {.style = rule.animation};
	if (!manager->expressiveAnimations()) return choice;

	if (!expressiveOnSoftware() && softwareRendering(this->window)) {
		static auto logged = false;
		if (!std::exchange(logged, true)) {
			qCInfo(logPanel) << "No GPU, expressive panel animations use the standard ones";
		}

		return choice;
	}

	choice.effect = resolvePanelEffect(rule.animation, rule.effect);
	return choice;
}

Qt::Edge WinPanelWindow::slideEdge(const PanelAnimationStyle& style) const {
	auto anchors = this->bAnchors.value();
	auto edges = Qt::Edges();
	if (anchors.mTop) edges |= Qt::TopEdge;
	if (anchors.mBottom) edges |= Qt::BottomEdge;
	if (anchors.mLeft) edges |= Qt::LeftEdge;
	if (anchors.mRight) edges |= Qt::RightEdge;

	auto panel = this->window != nullptr ? QRectF(this->window->geometry()) : QRectF();
	auto screen = this->mTrackedScreen != nullptr ? QRectF(this->mTrackedScreen->geometry()) : panel;
	return panelSlideEdge(style.edge, panel, screen, edges);
}

PanelAnimationTiming WinPanelWindow::animationTiming() const {
	if (this->mAnimationEffect == PanelEffect::Standard) return PanelAnimationTiming::hyprland();

	auto size = this->window != nullptr ? QSizeF(this->window->size()) : QSizeF();
	auto overshoot = panelSpringOvershoot(
	    this->mAnimationStyle,
	    this->mAnimationEffect,
	    this->mAnimationEdge,
	    size
	);
	return PanelAnimationTiming::expressive(this->mAnimationEffect, overshoot);
}

std::optional<qreal> WinPanelWindow::animationTravel() const {
	if (this->mAnimationEffect != PanelEffect::Reveal || this->window == nullptr) return std::nullopt;
	return panelRevealTravel(this->mAnimationEdge, QSizeF(this->window->size()));
}

void WinPanelWindow::prepareShowAnimation(const PanelAnimationChoice& choice) {
	this->mAnimationStyle = choice.style;
	this->mAnimationEffect = choice.effect;
	this->mAnimationEdge = this->slideEdge(choice.style);
	this->mAnimationState.jumpTo(false);
	this->blur->setSuspended(true);
	this->prepareEffectLayer();
	this->applyAnimationFrame();
}

void WinPanelWindow::prepareEffectLayer() {
	if (!panelEffectUsesShader(this->mAnimationEffect)) {
		this->mEffectLayer.reset();
		return;
	}

	if (this->mEffectLayer != nullptr && this->mEffectLayer->item() != nullptr) return;
	this->mEffectLayer.reset();

	auto* generation = EngineGeneration::findObjectGeneration(this);
	auto alive = generation != nullptr && generation->root != nullptr;
	auto* engine = alive ? generation->engine : nullptr;
	auto* root = this->window != nullptr ? this->window->contentItem() : nullptr;
	auto* source = this->contentItem();
	auto* renderer = this->window != nullptr ? this->window->rendererInterface() : nullptr;
	auto shaders =
	    renderer != nullptr && QSGRendererInterface::isApiRhiBased(renderer->graphicsApi());

	if (engine != nullptr && root != nullptr && source != nullptr && shaders) {
		this->mEffectLayer = PanelEffects::instance()->createLayer(
		    engine,
		    generation->root,
		    root,
		    source,
		    this->mAnimationEffect == PanelEffect::Blur
		);
	}

	if (this->mEffectLayer == nullptr) {
		qCDebug(logPanel) << "No effect layer for" << this << "so it animates the standard way";
		this->mAnimationEffect = PanelEffect::Standard;
	}
}

void WinPanelWindow::runAnimation(bool show) {
	if (this->mAnimation != nullptr) this->mAnimation->stop();
	this->cancelFrameWait();

	auto fromHidden =
	    show && this->mAnimationState.motion() <= 0 && this->mAnimationState.opacity() <= 0;
	if (fromHidden) this->mAnimationEdge = this->slideEdge(this->mAnimationStyle);
	this->mCascadePending = fromHidden && this->mAnimationEffect == PanelEffect::Cascade;

	this->mAnimationState.start(show, this->mAnimationStyle, this->animationTiming());

	if (this->mAnimation == nullptr) {
		this->mAnimation = new QVariantAnimation(this);
		this->mAnimation->setEasingCurve(QEasingCurve::Linear);

		QObject::connect(
		    this->mAnimation,
		    &QVariantAnimation::valueChanged,
		    this,
		    &WinPanelWindow::onAnimationTick
		);

		QObject::connect(
		    this->mAnimation,
		    &QAbstractAnimation::finished,
		    this,
		    &WinPanelWindow::onAnimationFinished
		);
	}

	if (this->mAnimationState.duration() <= 0 && !this->mCascadePending) {
		this->onAnimationFinished();
		return;
	}

	this->applyAnimationFrame();

	if (fromHidden && (this->window == nullptr || !this->window->isExposed())) {
		this->waitForFrames(1, FIRST_FRAME_TIMEOUT_MS, &WinPanelWindow::startAnimationClock);
		return;
	}

	this->runAnimationClock();
}

void WinPanelWindow::startAnimationClock() {
	if (this->mAnimation == nullptr || this->mAnimation->state() != QAbstractAnimation::Stopped) {
		return;
	}

	if (!this->mAnimationState.showing() || this->mAnimationState.settled()) return;
	this->runAnimationClock();
}

void WinPanelWindow::runAnimationClock() {
	if (std::exchange(this->mCascadePending, false)) this->beginCascade();

	auto duration = this->mAnimationState.duration();
	if (duration <= 0) {
		this->onAnimationFinished();
		return;
	}

	this->mAnimation->setStartValue(0.0);
	this->mAnimation->setEndValue(duration);
	this->mAnimation->setDuration(static_cast<int>(std::ceil(duration)));
	this->mAnimation->start();
}

void WinPanelWindow::beginCascade() {
	this->resetCascade();
	this->mAnimationState.startCascade(0);

	auto* content = this->contentItem();
	if (content == nullptr || this->window == nullptr) return;

	auto sections = panelCascadeSections(content, this->mAnimationEdge);
	if (sections.isEmpty()) return;

	auto size = QSizeF(this->window->size());
	this->mCascadeDistance = panelCascadeDistance(this->mAnimationEdge, size);
	this->mCascadeSpring = SpringCurve::withOvershoot(panelCascadeOvershoot(this->mCascadeDistance));

	for (auto* item: sections) {
		auto section = CascadeSection {
		    .item = item,
		    .transform = std::make_unique<PanelContentTransform>(),
		};

		section.transform->appendToItem(item);
		if (item->opacity() == 1) section.appliedOpacity = 1;
		this->mCascade.push_back(std::move(section));
	}

	auto count = static_cast<qsizetype>(this->mCascade.size());
	this->mAnimationState.startCascade(PanelCascadeTiming::expressive().durationFor(count));
	qCDebug(logPanel) << "Cascading" << count << "sections of" << this;

	this->applyCascadeFrame();
}

void WinPanelWindow::onAnimationTick(const QVariant& value) {
	if (this->mAnimation == nullptr || this->mAnimation->state() != QAbstractAnimation::Running) {
		return;
	}

	this->mAnimationState.advance(value.toReal());
	this->applyAnimationFrame();
}

void WinPanelWindow::onAnimationFinished() {
	this->mAnimationState.advance(this->mAnimationState.duration());
	this->applyAnimationFrame();

	if (this->mAnimationState.showing()) this->resetAnimation();
	else if (this->mClosing) this->hideAfterLastFrames();
}

void WinPanelWindow::applyAnimationFrame() {
	auto* item = this->window != nullptr ? this->window->contentItem() : nullptr;
	if (item == nullptr) return;

	if (item != this->mAnimatedItem) {
		this->mContentTransform.reset();
		if (this->mAnimatedItem != nullptr) this->mAnimatedItem->setOpacity(1);
		this->mAnimatedItem = item;
	}

	auto size = QSizeF(this->window->size());
	auto dpr = this->devicePixelRatio();
	auto frame = panelAnimationFrame(
	    this->mAnimationStyle,
	    this->mAnimationEdge,
	    size,
	    this->mAnimationState.motion(),
	    this->mAnimationState.opacity(),
	    dpr,
	    this->animationTravel()
	);

	if (frame.offset.isNull() && frame.scale == 1) {
		this->mContentTransform.reset();
	} else {
		if (this->mContentTransform == nullptr) {
			this->mContentTransform = std::make_unique<PanelContentTransform>();
			this->mContentTransform->appendToItem(item);
		}

		this->mContentTransform->setFrame(frame, size);
	}

	item->setOpacity(frame.opacity);

	if (this->mEffectLayer != nullptr) {
		auto effect = panelEffectFrame(
		    this->mAnimationEffect,
		    this->mAnimationEdge,
		    size,
		    this->mAnimationState.effect(),
		    frame.offset
		);

		this->mEffectLayer->apply(effect, size, dpr);
	}

	this->applyCascadeFrame();
}

void WinPanelWindow::applyCascadeFrame() {
	if (this->mCascade.empty()) return;

	const auto& timing = PanelCascadeTiming::expressive();
	auto elapsed = this->mAnimationState.cascadeTime();
	auto dpr = this->devicePixelRatio();

	for (std::size_t i = 0; i < this->mCascade.size(); i++) {
		auto& section = this->mCascade[i];
		auto* item = section.item.data();
		if (item == nullptr) continue;

		auto cascade = panelCascadeFrame(
		    timing,
		    this->mCascadeSpring,
		    static_cast<qsizetype>(i),
		    elapsed,
		    this->mAnimationEdge,
		    this->mCascadeDistance,
		    dpr
		);

		auto frame = PanelAnimationFrame();
		frame.offset = cascade.offset;
		section.transform->setFrame(frame, item->size());

		if (section.appliedOpacity < 0) continue;

		if (item->opacity() != section.appliedOpacity) {
			section.appliedOpacity = -1;
			continue;
		}

		item->setOpacity(cascade.opacity);
		section.appliedOpacity = item->opacity();
	}
}

void WinPanelWindow::setAnimationTransformsBypassed(bool bypassed) {
	if (this->mContentTransform != nullptr) this->mContentTransform->setBypassed(bypassed);

	for (auto& section: this->mCascade) {
		section.transform->setBypassed(bypassed);
	}
}

void WinPanelWindow::resetCascade() {
	for (auto& section: this->mCascade) {
		auto* item = section.item.data();
		if (item == nullptr || section.appliedOpacity < 0) continue;
		if (item->opacity() == section.appliedOpacity) item->setOpacity(1);
	}

	this->mCascade.clear();
	this->mCascadePending = false;
}

void WinPanelWindow::resetAnimatedItem() {
	this->resetCascade();
	this->mEffectLayer.reset();
	this->mContentTransform.reset();
	if (this->mAnimatedItem != nullptr) this->mAnimatedItem->setOpacity(1);
	this->mAnimatedItem = nullptr;
}

void WinPanelWindow::resetAnimation() {
	if (this->mAnimation != nullptr) this->mAnimation->stop();
	this->cancelFrameWait();

	this->mAnimationState.jumpTo(true);
	this->resetAnimatedItem();
	this->blur->setSuspended(false);
}

void WinPanelWindow::beginClose(const PanelAnimationChoice& choice) {
	if (this->mAnimationState.settled()) {
		this->mAnimationStyle = choice.style;
		this->mAnimationEffect = choice.effect;
		this->mAnimationEdge = this->slideEdge(choice.style);
		this->prepareEffectLayer();
	}

	qCDebug(logPanel) << "Closing" << this << "with" << this->mAnimationStyle.toString()
	                  << panelEffectName(this->mAnimationEffect);

	this->mClosing = true;
	this->focusOnExpose = false;
	this->appBar.remove();
	this->routeInputMask();
	this->releaseForegroundForClose();
	this->blur->setSuspended(true);
	this->runAnimation(false);

	this->bBackerVisibility = false;
	emit this->visibleChanged();
}

void WinPanelWindow::reopen() {
	qCDebug(logPanel) << "Reopening" << this << "while it closes";

	this->mClosing = false;
	this->routeInputMask();

	if (this->animationsAllowed()) this->runAnimation(true);
	else this->resetAnimation();

	this->bBackerVisibility = true;
	this->onWindowVisibleChanged();
	if (this->pendingPolish.inputMask) this->schedulePolish();
	emit this->visibleChanged();
}

void WinPanelWindow::waitForFrames(int frames, int timeoutMs, void (WinPanelWindow::*done)()) {
	this->cancelFrameWait();

	auto* window = this->window;
	if (window == nullptr || !window->isVisible()) {
		(this->*done)();
		return;
	}

	auto request = this->mFrameWaitRequest;
	this->mFramesLeft = frames;

	this->mFrameWait = QObject::connect(
	    window,
	    &QQuickWindow::frameSwapped,
	    this,
	    [this, request, done]() {
		    if (request != this->mFrameWaitRequest) return;

		    if (--this->mFramesLeft > 0) {
			    if (this->window != nullptr) this->window->update();
			    return;
		    }

		    this->cancelFrameWait();
		    (this->*done)();
	    },
	    Qt::QueuedConnection
	);

	QTimer::singleShot(timeoutMs, this, [this, request, done]() {
		if (request != this->mFrameWaitRequest) return;
		this->cancelFrameWait();
		(this->*done)();
	});

	window->update();
}

void WinPanelWindow::cancelFrameWait() {
	this->mFrameWaitRequest++;
	if (this->mFrameWait) QObject::disconnect(this->mFrameWait);
	this->mFrameWait = {};
}

void WinPanelWindow::hideAfterLastFrames() {
	auto* window = this->window;

	if (window == nullptr || !window->isVisible() || !window->isExposed()) {
		this->finishClose();
		return;
	}

	this->waitForFrames(CLOSE_FRAMES, CLOSE_FRAME_TIMEOUT_MS, &WinPanelWindow::finishClose);
}

void WinPanelWindow::finishClose() {
	if (!this->mClosing) return;

	this->mClosing = false;
	this->cancelFrameWait();

	if (this->window != nullptr && this->window->isVisible()) {
		this->quietVisibilityChange = true;
		this->window->setVisible(false);
		this->quietVisibilityChange = false;
	}

	this->resetAnimation();
	this->routeInputMask();
	this->scheduleUpdateDimensions();
}

void WinPanelWindow::releaseForegroundForClose() {
	auto* hwnd = this->hwnd();
	if (hwnd == nullptr || !ownsForeground(hwnd)) return;

	auto* next = nextActivatableWindow(hwnd);
	if (next == nullptr) next = GetShellWindow();
	if (next == nullptr) return;

	qCDebug(logPanel) << "Handing keyboard focus from" << this << "to" << next << "as it closes";
	SetForegroundWindow(next);
}

WinPanelInterface::WinPanelInterface(QObject* parent)
    : PanelWindowInterface(parent)
    , panel(new WinPanelWindow(this)) {
	this->connectSignals();

	// clang-format off
	QObject::connect(this->panel, &WinPanelWindow::anchorsChanged, this, &WinPanelInterface::anchorsChanged);
	QObject::connect(this->panel, &WinPanelWindow::marginsChanged, this, &WinPanelInterface::marginsChanged);
	QObject::connect(this->panel, &WinPanelWindow::exclusiveZoneChanged, this, &WinPanelInterface::exclusiveZoneChanged);
	QObject::connect(this->panel, &WinPanelWindow::exclusionModeChanged, this, &WinPanelInterface::exclusionModeChanged);
	QObject::connect(this->panel, &WinPanelWindow::layerChanged, this, &WinPanelInterface::aboveWindowsChanged);
	QObject::connect(this->panel, &WinPanelWindow::keyboardFocusChanged, this, &WinPanelInterface::focusableChanged);
	// clang-format on
}

void WinPanelInterface::onReload(QObject* oldInstance) {
	QQmlEngine::setContextForObject(this->panel, QQmlEngine::contextForObject(this));

	auto* old = qobject_cast<WinPanelInterface*>(oldInstance);
	this->panel->reload(old != nullptr ? old->panel : nullptr);
}

ProxyWindowBase* WinPanelInterface::proxyWindow() const { return this->panel; }

// NOLINTBEGIN
#define proxyPair(type, get, set)                                                                  \
	type WinPanelInterface::get() const { return this->panel->get(); }                               \
	void WinPanelInterface::set(type value) { this->panel->set(value); }

proxyPair(Anchors, anchors, setAnchors);
proxyPair(Margins, margins, setMargins);
proxyPair(qint32, exclusiveZone, setExclusiveZone);
proxyPair(ExclusionMode::Enum, exclusionMode, setExclusionMode);
proxyPair(bool, focusable, setFocusable);
proxyPair(bool, aboveWindows, setAboveWindows);

#undef proxyPair
// NOLINTEND

} // namespace qs::windows
