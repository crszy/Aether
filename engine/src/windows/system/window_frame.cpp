#include "window_frame.hpp"

#include <algorithm>

#include <qmargins.h>
#include <qobject.h>
#include <qt_windows.h>
#include <qvariant.h>
#include <qwindow.h>
#include <qguiapplication.h>
#include <qpixmap.h>
#include <qscreen.h>
#include <qurl.h>
#include <qpa/qplatformwindow_p.h>

#include <dwmapi.h>

#include "../util.hpp"

namespace qs::windows::sys {

namespace {

constexpr auto HIDDEN_PROPERTY = "_qs_hiddenTitleBar";
constexpr auto WATCHED_PROPERTY = "_qs_titleBarWatched";

QMargins hiddenMargins(QWindow* window) {
	auto* hwnd = reinterpret_cast<HWND>(window->winId()); // NOLINT(performance-no-int-to-ptr)
	auto style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
	auto exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
	RECT frame {};
	if (!AdjustWindowRectExForDpi(&frame, style, FALSE, exStyle, GetDpiForWindow(hwnd))) return {};

	auto top = -frame.top;
	if (IsZoomed(hwnd)) top -= frame.bottom;
	return {0, -static_cast<int>(top), 0, 0};
}

bool applyMargins(QWindow* window, bool keepFrame = false) {
	auto* native = window->nativeInterface<QNativeInterface::Private::QWindowsWindow>();
	if (native == nullptr) return false;

	auto margins = window->property(HIDDEN_PROPERTY).toBool() ? hiddenMargins(window) : QMargins();
	if (native->customMargins() == margins) return true;

	auto* hwnd = reinterpret_cast<HWND>(window->winId()); // NOLINT(performance-no-int-to-ptr)
	RECT frame {};
	keepFrame = keepFrame && !IsZoomed(hwnd) && !IsIconic(hwnd) && GetWindowRect(hwnd, &frame);
	native->setCustomMargins(margins);
	if (keepFrame) {
		SetWindowPos(
		    hwnd,
		    nullptr,
		    frame.left,
		    frame.top,
		    frame.right - frame.left,
		    frame.bottom - frame.top,
		    SWP_NOZORDER | SWP_NOACTIVATE
		);
	}
	return true;
}

void keepOnScreen(QWindow* window) {
	auto* hwnd = reinterpret_cast<HWND>(window->winId()); // NOLINT(performance-no-int-to-ptr)
	if (IsZoomed(hwnd) || IsIconic(hwnd)) return;

	MONITORINFO monitor {.cbSize = sizeof(MONITORINFO)};
	RECT frame {};
	if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) return;
	if (!GetWindowRect(hwnd, &frame)) return;

	const auto& work = monitor.rcWork;
	auto width = std::min(frame.right - frame.left, work.right - work.left);
	auto height = std::min(frame.bottom - frame.top, work.bottom - work.top);
	auto left = std::clamp(frame.left, work.left, work.right - width);
	auto top = std::clamp(frame.top, work.top, work.bottom - height);
	if (left == frame.left && top == frame.top && width == frame.right - frame.left
	    && height == frame.bottom - frame.top)
	{
		return;
	}

	SetWindowPos(hwnd, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace

WindowFrame::WindowFrame(QObject* parent): QObject(parent) {}

bool WindowFrame::hideTitleBar(QObject* object) {
	auto* window = qobject_cast<QWindow*>(object);
	if (window == nullptr) return false;

	window->setProperty(HIDDEN_PROPERTY, true);
	if (!window->property(WATCHED_PROPERTY).toBool()) {
		window->setProperty(WATCHED_PROPERTY, true);
		QObject::connect(window, &QWindow::windowStateChanged, window, [window] { applyMargins(window, true); });
		QObject::connect(window, &QWindow::screenChanged, window, [window] { applyMargins(window); });
	}

	window->winId();
	if (!applyMargins(window)) return false;
	keepOnScreen(window);
	return true;
}

bool WindowFrame::bringToFront(QObject* object) {
	auto* window = qobject_cast<QWindow*>(object);
	if (window == nullptr) return false;
	auto* hwnd = reinterpret_cast<HWND>(window->winId()); // NOLINT(performance-no-int-to-ptr)
	if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
	window->raise();
	BringWindowToTop(hwnd);
	auto ok = qs::windows::forceForegroundWindow(hwnd);
	window->requestActivate();
	return ok;
}

bool WindowFrame::setTopmost(QObject* object, bool topmost) {
	auto* window = qobject_cast<QWindow*>(object);
	if (window == nullptr) return false;
	auto* hwnd = reinterpret_cast<HWND>(window->winId()); // NOLINT(performance-no-int-to-ptr)
	return SetWindowPos(
	           hwnd,
	           topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
	           0,
	           0,
	           0,
	           0,
	           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
	       )
	    != FALSE;
}

bool WindowFrame::setBackdrop(QObject* object, const QString& kind, bool dark) {
	auto* window = qobject_cast<QWindow*>(object);
	if (window == nullptr) return false;
	auto* hwnd = reinterpret_cast<HWND>(window->winId()); // NOLINT(performance-no-int-to-ptr)

	constexpr DWORD ATTR_DARK_MODE = 20;     // DWMWA_USE_IMMERSIVE_DARK_MODE
	constexpr DWORD ATTR_BACKDROP_TYPE = 38; // DWMWA_SYSTEMBACKDROP_TYPE (Windows 11 22H2+)
	BOOL useDark = dark ? TRUE : FALSE;
	DwmSetWindowAttribute(hwnd, ATTR_DARK_MODE, &useDark, sizeof(useDark));

	// "blur": the accent-policy blur the shell's panels use - unlike acrylic/mica it stays blurred while the
	// window is not focused (Windows 11 turns those into a flat colour then)
	{
		struct AccentPolicy {
			DWORD accentState;
			DWORD accentFlags;
			DWORD gradientColor;
			DWORD animationId;
		};
		struct CompositionData {
			DWORD attribute;
			PVOID data;
			SIZE_T dataSize;
		};
		using SetFn = BOOL(WINAPI*)(HWND, CompositionData*);
		static const auto setComposition = reinterpret_cast<SetFn>( // NOLINT
		    GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute")
		);
		if (setComposition != nullptr) {
			constexpr DWORD WCA_ACCENT_POLICY = 19;
			constexpr DWORD ACCENT_DISABLED = 0;
			constexpr DWORD ACCENT_ENABLE_BLURBEHIND = 3;
			auto policy = AccentPolicy {
			    kind == QStringLiteral("blur") ? ACCENT_ENABLE_BLURBEHIND : ACCENT_DISABLED,
			    0,
			    0,
			    0
			};
			auto data = CompositionData {WCA_ACCENT_POLICY, &policy, sizeof(policy)};
			setComposition(hwnd, &data);
		}
		if (kind == QStringLiteral("blur")) {
			int none = 1;
			DwmSetWindowAttribute(hwnd, 38, &none, sizeof(none));
			// the see-through parts of the window show the frame, which the accent blur draws
			MARGINS all {-1, -1, -1, -1};
			DwmExtendFrameIntoClientArea(hwnd, &all);
			return setComposition != nullptr;
		}
	}

	int type = 1; // DWMSBT_NONE
	if (kind == QStringLiteral("mica")) type = 2;
	else if (kind == QStringLiteral("acrylic")) type = 3;
	else if (kind == QStringLiteral("tabbed")) type = 4;

	// the backdrop shows through the whole client area only when the frame is extended over it
	MARGINS margins {-1, -1, -1, -1};
	if (type == 1) margins = {0, 0, 0, 0};
	DwmExtendFrameIntoClientArea(hwnd, &margins);
	return SUCCEEDED(DwmSetWindowAttribute(hwnd, ATTR_BACKDROP_TYPE, &type, sizeof(type)));
}

bool WindowFrame::excludeFromCapture(QObject* object, bool exclude) {
	auto* window = qobject_cast<QWindow*>(object);
	if (window == nullptr) return false;
	auto* hwnd = reinterpret_cast<HWND>(window->winId()); // NOLINT(performance-no-int-to-ptr)
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
	return SetWindowDisplayAffinity(hwnd, exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE) != FALSE;
}

QString WindowFrame::grabScreen(const QString& screenName, const QString& path) {
	for (auto* screen: QGuiApplication::screens()) {
		if (screen->name() != screenName) continue;
		auto shot = screen->grabWindow(0);
		if (shot.isNull() || !shot.save(path, "PNG")) return {};
		return QUrl::fromLocalFile(path).toString();
	}
	return {};
}

bool WindowFrame::showTitleBar(QObject* object) {
	auto* window = qobject_cast<QWindow*>(object);
	if (window == nullptr) return false;

	window->setProperty(HIDDEN_PROPERTY, false);
	return applyMargins(window);
}

} // namespace qs::windows::sys
