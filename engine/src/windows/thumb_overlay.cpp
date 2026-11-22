#include "thumb_overlay.hpp"

#include <cmath>

#include <qlogging.h>
#include <qloggingcategory.h>

#include <dwmapi.h>

namespace qs::windows {

namespace {
Q_LOGGING_CATEGORY(logOverlay, "quickshell.windows.thumboverlay", QtWarningMsg);

constexpr const wchar_t* OVERLAY_CLASS = L"CaelestiaAnimationOverlay";

LRESULT CALLBACK overlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_NCHITTEST: return HTTRANSPARENT;
	case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
	case WM_ERASEBKGND: return 1;
	default: return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}

QRect frameOf(HWND hwnd, QRect& raw) {
	RECT r {};
	GetWindowRect(hwnd, &r);
	raw = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
	RECT f {};
	if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &f, sizeof(f)))) return raw;
	return QRect(f.left, f.top, f.right - f.left, f.bottom - f.top);
}

} // namespace

ThumbOverlay::~ThumbOverlay() {
	this->finish();
	if (this->overlay != nullptr) DestroyWindow(this->overlay);
}

bool ThumbOverlay::ensureWindow() {
	if (this->overlay != nullptr) return true;

	static bool registered = false;
	if (!registered) {
		WNDCLASSEXW wc {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = overlayProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = OVERLAY_CLASS;
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		registered = RegisterClassExW(&wc) != 0;
	}

	// layered + transparent: every click goes straight through to the real windows below
	this->overlay = CreateWindowExW(
	    WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
	    OVERLAY_CLASS,
	    L"",
	    WS_POPUP,
	    0,
	    0,
	    1,
	    1,
	    nullptr,
	    nullptr,
	    GetModuleHandleW(nullptr),
	    nullptr
	);
	if (this->overlay == nullptr) {
		qCWarning(logOverlay) << "Could not create the animation overlay:" << GetLastError();
		return false;
	}
	SetLayeredWindowAttributes(this->overlay, 0, 255, LWA_ALPHA);
	// it must never show up in task switchers or as a thumbnail itself
	BOOL excluded = TRUE;
	DwmSetWindowAttribute(this->overlay, DWMWA_EXCLUDED_FROM_PEEK, &excluded, sizeof(excluded));
	return true;
}

bool ThumbOverlay::registerThumb(HWND hwnd, Thumb& thumb) {
	if (FAILED(DwmRegisterThumbnail(this->overlay, hwnd, &thumb.handle))) {
		thumb.handle = nullptr;
		return false;
	}
	return true;
}

void ThumbOverlay::place(const Thumb& thumb, const QRect& rect) const {
	DWM_THUMBNAIL_PROPERTIES props {};
	props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_VISIBLE | DWM_TNP_OPACITY
	              | DWM_TNP_SOURCECLIENTAREAONLY;
	props.rcDestination = RECT {
	    rect.left() - this->desktop.left(),
	    rect.top() - this->desktop.top(),
	    rect.left() - this->desktop.left() + rect.width(),
	    rect.top() - this->desktop.top() + rect.height(),
	};
	props.rcSource = RECT {
	    thumb.source.left(),
	    thumb.source.top(),
	    thumb.source.left() + thumb.source.width(),
	    thumb.source.top() + thumb.source.height(),
	};
	props.fVisible = TRUE;
	props.opacity = 255;
	props.fSourceClientAreaOnly = FALSE;
	DwmUpdateThumbnailProperties(thumb.handle, &props);
}

// every visible window, bottom to top, as it is right now
void ThumbOverlay::snapshot() {
	QList<HWND> topToBottom;
	EnumWindows(
	    [](HWND hwnd, LPARAM param) -> BOOL {
		    reinterpret_cast<QList<HWND>*>(param)->append(hwnd); // NOLINT(performance-no-int-to-ptr)
		    return TRUE;
	    },
	    reinterpret_cast<LPARAM>(&topToBottom)
	);

	for (auto i = topToBottom.size() - 1; i >= 0; i--) {
		auto* hwnd = topToBottom.at(i);
		if (hwnd == this->overlay || !IsWindowVisible(hwnd) || IsIconic(hwnd)) continue;
		DWORD cloaked = 0;
		DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
		if (cloaked != 0) continue;

		QRect raw;
		auto frame = frameOf(hwnd, raw);
		if (frame.width() <= 1 || frame.height() <= 1 || !frame.intersects(this->desktop)) continue;

		Thumb thumb;
		thumb.from = frame;
		thumb.to = frame;
		thumb.source = QRect(frame.topLeft() - raw.topLeft(), frame.size());
		if (!this->registerThumb(hwnd, thumb)) continue;
		this->place(thumb, frame);
		this->order.append(hwnd);
		this->thumbs.insert(hwnd, thumb);
	}
}

bool ThumbOverlay::add(HWND hwnd, const QRect& from, const QRect& to) {
	if (!this->shown) {
		if (!this->ensureWindow()) return false;

		this->desktop = QRect(
		    GetSystemMetrics(SM_XVIRTUALSCREEN),
		    GetSystemMetrics(SM_YVIRTUALSCREEN),
		    GetSystemMetrics(SM_CXVIRTUALSCREEN),
		    GetSystemMetrics(SM_CYVIRTUALSCREEN)
		);
		SetWindowPos(
		    this->overlay,
		    HWND_TOPMOST,
		    this->desktop.left(),
		    this->desktop.top(),
		    this->desktop.width(),
		    this->desktop.height(),
		    SWP_NOACTIVATE
		);
		this->snapshot();
		if (this->thumbs.isEmpty()) return false;

		ShowWindow(this->overlay, SW_SHOWNA);
		// wait until the overlay is on screen, so moving the real windows below it can't flash
		DwmFlush();
		this->shown = true;
	}

	auto iter = this->thumbs.find(hwnd);
	if (iter == this->thumbs.end()) return false;   // it was not visible when the animation began
	iter->from = from;
	iter->to = to;
	iter->moving = true;
	this->place(*iter, from);
	return true;
}

void ThumbOverlay::tick(double e) {
	if (!this->shown) return;
	auto lerp = [e](int a, int b) { return int(std::lround(a + (b - a) * e)); };
	for (const auto& thumb: this->thumbs) {
		if (!thumb.moving) continue;
		auto rect = QRect(
		    lerp(thumb.from.left(), thumb.to.left()),
		    lerp(thumb.from.top(), thumb.to.top()),
		    qMax(1, lerp(thumb.from.width(), thumb.to.width())),
		    qMax(1, lerp(thumb.from.height(), thumb.to.height()))
		);
		this->place(thumb, rect);
	}
}

void ThumbOverlay::finish() {
	if (this->overlay != nullptr && this->shown) ShowWindow(this->overlay, SW_HIDE);
	for (const auto& thumb: this->thumbs) {
		if (thumb.handle != nullptr) DwmUnregisterThumbnail(thumb.handle);
	}
	this->thumbs.clear();
	this->order.clear();
	this->shown = false;
}

} // namespace qs::windows
