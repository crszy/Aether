#pragma once

#include <qt_windows.h>
#include <functional>

#include <qpoint.h>
#include <qrect.h>
#include <qscreen.h>
#include <qtypes.h>
#include <qwindow.h>

// SystemParametersInfoW without the blocking broadcast: SPIF_SENDCHANGE sends WM_SETTINGCHANGE to every top-level
// window and waits on each, so one hung app anywhere froze the shell ("Not responding", 2026-11-08). The setting is
// written without it, then apps are told with SendNotifyMessage, which never waits. Same result for apps.
inline BOOL systemParametersNoWait(UINT action, UINT uiParam, PVOID pvParam, UINT flags) {
	const BOOL ok = SystemParametersInfoW(action, uiParam, pvParam, flags & ~static_cast<UINT>(SPIF_SENDCHANGE));
	if (ok && (flags & SPIF_SENDCHANGE)) SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, action, 0);
	return ok;
}

namespace qs::windows {

struct MonitorRects {
	QRect monitor;
	QRect work;
	bool valid = false;
};

[[nodiscard]] HMONITOR monitorForScreen(QScreen* screen);
[[nodiscard]] QScreen* screenForMonitor(HMONITOR monitor);
[[nodiscard]] QScreen* focusedScreen();
void setFocusedScreen(QScreen* screen);
void prioritizeInputThread(int priority);
void optOutOfProcessPowerThrottling();
[[nodiscard]] MonitorRects monitorRects(HMONITOR monitor);

class ScreenMapper {
public:
	ScreenMapper(const QRect& logicalScreen, const QRect& physicalMonitor, qreal dpr)
	    : logicalOrigin(logicalScreen.topLeft())
	    , physicalOrigin(physicalMonitor.topLeft())
	    , dpr(dpr) {}

	[[nodiscard]] QPoint toLogical(const QPoint& physical) const;
	[[nodiscard]] QRect toLogical(const QRect& physical) const;
	[[nodiscard]] QPoint toPhysical(const QPoint& logical) const;
	[[nodiscard]] QRect toPhysical(const QRect& logical) const;
	[[nodiscard]] qint32 toPhysical(qint32 logicalLength) const;

private:
	QPoint logicalOrigin;
	QPoint physicalOrigin;
	qreal dpr;
};

[[nodiscard]] HWND hwndOf(const QWindow* window);
[[nodiscard]] QRect toQRect(const RECT& rect);
[[nodiscard]] RECT toRECT(const QRect& rect);

bool forceForegroundWindow(HWND hwnd);
void runLauncher(std::function<void()> task);

DWORD windowsBuild();

[[nodiscard]] bool isOwnProcessWindow(HWND hwnd);
[[nodiscard]] bool isMoreElevated(HWND hwnd);

inline constexpr const wchar_t* TRAY_HOOK_PROP = L"QuickshellTrayHook";
[[nodiscard]] bool isTrayHookWindow(HWND hwnd);

[[nodiscard]] HWND explorerTaskbarWindow();

class TrayHookYield {
public:
	TrayHookYield();
	~TrayHookYield();
	TrayHookYield(const TrayHookYield&) = delete;
	TrayHookYield& operator=(const TrayHookYield&) = delete;
	TrayHookYield(TrayHookYield&&) = delete;
	TrayHookYield& operator=(TrayHookYield&&) = delete;
};

[[nodiscard]] bool trayHookYielding();

void markNonRude(HWND hwnd);

void applyPanelDwmAttributes(HWND hwnd);

void setExStyleBits(HWND hwnd, LONG_PTR bits, bool enabled);

} // namespace qs::windows
