#include "caption_skin.hpp"

#include <array>

#include <qcolor.h>
#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qstring.h>
#include <qtimer.h>
#include <qwindow.h>

#include <dwmapi.h>

#include "../util.hpp"
#include "../window_decorations.hpp"

namespace qs::windows::sys {

namespace {

constexpr int POLL_MS = 60;
constexpr int DRAG_MS = 8;
constexpr int MIN_HEADER = 8;   // physical px: anything thinner is not a title bar
constexpr int MIN_HEIGHT = 120; // physical px: smallest height resizing the top edge leaves
constexpr int CORNER = 8;       // Windows 11 window corner radius at 96 dpi
constexpr UINT WM_POPUPSYSTEMMENU = 0x0313;
constexpr UINT ASYNC_MOVE = SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS;

// every live skin, by the app window it belongs to (one per window)
QHash<HWND, CaptionSkin*>& skins() {
	static QHash<HWND, CaptionSkin*> map; // NOLINT
	return map;
}

HWINEVENTHOOK gMoveHook = nullptr;       // NOLINT
HWINEVENTHOOK gForegroundHook = nullptr; // NOLINT

void CALLBACK onMoved(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
	if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || hwnd == nullptr) return;
	auto* skin = skins().value(hwnd);
	if (skin != nullptr) skin->place();
}

void CALLBACK onForeground(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
	for (auto* skin: skins()) skin->poll();
}

void updateHooks() {
	auto any = !skins().isEmpty();
	if (any && gMoveHook == nullptr) {
		gMoveHook = SetWinEventHook(
		    EVENT_OBJECT_LOCATIONCHANGE,
		    EVENT_OBJECT_LOCATIONCHANGE,
		    nullptr,
		    onMoved,
		    0,
		    0,
		    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
		);
		gForegroundHook = SetWinEventHook(
		    EVENT_SYSTEM_FOREGROUND,
		    EVENT_SYSTEM_MINIMIZEEND,
		    nullptr,
		    onForeground,
		    0,
		    0,
		    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
		);
	} else if (!any && gMoveHook != nullptr) {
		UnhookWinEvent(gMoveHook);
		if (gForegroundHook != nullptr) UnhookWinEvent(gForegroundHook);
		gMoveHook = nullptr;
		gForegroundHook = nullptr;
	}
}

bool cloaked(HWND hwnd) {
	DWORD value = 0;
	return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &value, sizeof(value))) && value != 0;
}

QString exePathOf(HWND hwnd) {
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	auto* process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (process == nullptr) return {};
	std::array<wchar_t, MAX_PATH * 2> buffer {};
	auto size = static_cast<DWORD>(buffer.size());
	QString path;
	if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) {
		path = QString::fromWCharArray(buffer.data(), static_cast<int>(size));
	}
	CloseHandle(process);
	return path;
}

// the visible frame (without Windows' invisible resize borders)
RECT frameOf(HWND hwnd) {
	RECT frame {};
	if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) {
		GetWindowRect(hwnd, &frame);
	}
	return frame;
}

MONITORINFO monitorOf(HWND hwnd) {
	MONITORINFO info {};
	info.cbSize = sizeof(info);
	GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info);
	return info;
}

MONITORINFO monitorAt(POINT point) {
	MONITORINFO info {};
	info.cbSize = sizeof(info);
	GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info);
	return info;
}

} // namespace

CaptionSkin::CaptionSkin(QObject* parent): QObject(parent) {
	this->timer.setInterval(POLL_MS);
	QObject::connect(&this->timer, &QTimer::timeout, this, &CaptionSkin::poll);
	this->dragTimer.setInterval(DRAG_MS);
	QObject::connect(&this->dragTimer, &QTimer::timeout, this, &CaptionSkin::dragStep);
}

CaptionSkin::~CaptionSkin() { this->detach(); }

void CaptionSkin::setWindow(QObject* window) {
	auto* qwindow = qobject_cast<QWindow*>(window);
	if (qwindow == this->mWindow.data()) return;
	this->detach();
	this->mWindow = qwindow;
	this->attach();
	emit this->windowChanged();
}

void CaptionSkin::setTarget(const QString& target) {
	if (target == this->mTarget) return;
	this->detach();
	this->mTarget = target;
	bool ok = false;
	auto value = target.toULongLong(&ok);
	this->mHwnd = ok ? reinterpret_cast<HWND>(static_cast<quintptr>(value)) : nullptr; // NOLINT
	this->mExePath = this->mHwnd != nullptr ? exePathOf(this->mHwnd) : QString();
	this->attach();
	emit this->targetChanged();
}

HWND CaptionSkin::ownHwnd() const {
	if (this->mWindow.isNull()) return nullptr;
	return reinterpret_cast<HWND>(this->mWindow->winId()); // NOLINT(performance-no-int-to-ptr)
}

// the height Windows' title bar had on this window (its style no longer has one)
int CaptionSkin::headerPx() const { return DecorationManager::headerPx(this->mAttached); }

// how round Windows makes the app's corners (Windows 11; never when maximised)
int CaptionSkin::cornerPx() const {
	auto* hwnd = this->mAttached;
	if (hwnd == nullptr || this->mMaximized || IsZoomed(hwnd) || windowsBuild() < 22000) return 0;
	DWORD preference = DWMWCP_DEFAULT;
	DwmGetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
	if (preference == DWMWCP_DONOTROUND) return 0;
	auto dpi = GetDpiForWindow(hwnd);
	return MulDiv(preference == DWMWCP_ROUNDSMALL ? 4 : CORNER, static_cast<int>(dpi == 0 ? 96 : dpi), 96);
}

// the colour along the top of the app's content: three points on the screen just under the header (only where
// the app is what is showing there); two that agree win, else the middle one
void CaptionSkin::sampleColor() {
	auto* hwnd = this->mAttached;
	if (hwnd == nullptr || !this->mShown) return;
	auto frame = frameOf(hwnd);
	auto y = frame.top + this->cornerPx() + 3;
	std::array<LONG, 3> xs {frame.left + 12, (frame.left + frame.right) / 2, frame.right - 12};
	QList<QRgb> seen;
	auto* dc = GetDC(nullptr);
	for (auto x: xs) {
		POINT point {x, y};
		auto* under = WindowFromPoint(point);
		if (under == nullptr || GetAncestor(under, GA_ROOT) != hwnd) continue;
		auto pixel = GetPixel(dc, x, y);
		if (pixel != CLR_INVALID) seen.append(qRgb(GetRValue(pixel), GetGValue(pixel), GetBValue(pixel)));
	}
	ReleaseDC(nullptr, dc);
	if (seen.isEmpty()) return;
	auto pick = seen.size() == 3 && seen[0] != seen[1] && seen[1] != seen[2] && seen[0] == seen[2] ? seen[0]
	          : seen.size() >= 2 && seen[0] == seen[1]                                         ? seen[0]
	          : seen[seen.size() / 2];
	QColor color(pick);
	if (color != this->mAppColor) {
		this->mAppColor = color;
		emit this->appColorChanged();
	}
}

void CaptionSkin::setBorderColor(const QColor& color) {
	if (color == this->mBorderColor) return;
	this->mBorderColor = color;
	this->applyBorder();
	emit this->borderColorChanged();
}

void CaptionSkin::applyBorder() {
	auto* hwnd = this->mAttached;
	if (hwnd == nullptr || windowsBuild() < 22000) return;
	if (this->mBorderColor.isValid() && this->mBorderColor.alpha() > 0) {
		auto rgb = this->mBorderColor.toRgb();
		COLORREF value = RGB(rgb.red(), rgb.green(), rgb.blue());
		DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &value, sizeof(value));
		// the thin top edge Windows still draws on a window with no title bar takes the caption colour
		DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &value, sizeof(value));
		this->borderSet = true;
	} else if (this->borderSet) {
		COLORREF value = DWMWA_COLOR_DEFAULT;
		DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &value, sizeof(value));
		DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &value, sizeof(value));
		this->borderSet = false;
	}
}

void CaptionSkin::attach() {
	if (this->mWindow.isNull() || this->mHwnd == nullptr || !IsWindow(this->mHwnd)) return;
	if (skins().contains(this->mHwnd) && skins().value(this->mHwnd) != this) return;

	skins().insert(this->mHwnd, this);
	this->mAttached = this->mHwnd;
	updateHooks();

	this->placed = {};
	this->applyBorder();
	this->poll();
	this->sampleColor();
	this->timer.start();
}

void CaptionSkin::detach() {
	this->timer.stop();
	this->dragTimer.stop();
	this->drag = Drag::None;

	// a window we maximised goes back to its size, so the restored title bar fits again
	if (this->mAttached != nullptr && this->mMaximized && IsWindow(this->mAttached)) this->maximizeTo(false);

	if (this->mAttached != nullptr && this->borderSet && IsWindow(this->mAttached)) {
		COLORREF value = DWMWA_COLOR_DEFAULT;
		DwmSetWindowAttribute(this->mAttached, DWMWA_BORDER_COLOR, &value, sizeof(value));
		DwmSetWindowAttribute(this->mAttached, DWMWA_CAPTION_COLOR, &value, sizeof(value));
		this->borderSet = false;
	}

	if (this->mAttached != nullptr) {
		if (skins().value(this->mAttached) == this) skins().remove(this->mAttached);
		updateHooks();
	}

	// never leave the app as our window's owner: the shell window would go away with it
	auto* own = this->ownHwnd();
	if (own != nullptr && IsWindow(own) && GetWindow(own, GW_OWNER) != nullptr) {
		SetWindowLongPtrW(own, GWLP_HWNDPARENT, 0);
	}
	this->mAttached = nullptr;

	if (this->mShown) {
		this->mShown = false;
		emit this->stateChanged();
	}
}

void CaptionSkin::poll() {
	auto* hwnd = this->mAttached;
	auto* own = this->ownHwnd();
	if (hwnd == nullptr || own == nullptr) return;

	if (!IsWindow(hwnd)) {
		this->detach();
		return;
	}

	// owned tool window: above the app, out of the taskbar and Alt+Tab, never takes focus itself
	if (GetWindow(own, GW_OWNER) != hwnd) SetWindowLongPtrW(own, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(hwnd));
	auto ex = GetWindowLongPtrW(own, GWL_EXSTYLE);
	auto wanted = (ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
	if (wanted != ex) SetWindowLongPtrW(own, GWL_EXSTYLE, wanted);

	// Windows maximised it (Win+Up, the app itself): that would cover the header's space, so make it ours
	if (IsZoomed(hwnd) && this->drag == Drag::None) {
		WINDOWPLACEMENT placement {};
		placement.length = sizeof(placement);
		GetWindowPlacement(hwnd, &placement);
		placement.showCmd = SW_SHOWNOACTIVATE;
		SetWindowPlacement(hwnd, &placement);
		GetWindowRect(hwnd, &this->normal);
		this->maximizeTo(true);
		return;
	}

	auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
	auto canMin = (style & WS_MINIMIZEBOX) != 0;
	auto canMax = (style & WS_MAXIMIZEBOX) != 0 && (style & WS_THICKFRAME) != 0;
	auto active = GetForegroundWindow() == hwnd;

	std::array<wchar_t, 512> text {};
	auto length = InternalGetWindowText(hwnd, text.data(), static_cast<int>(text.size()));
	auto title = QString::fromWCharArray(text.data(), length);

	this->place();

	// fullscreen (covers its monitor): no title bar, like before
	auto frame = frameOf(hwnd);
	auto monitor = monitorOf(hwnd).rcMonitor;
	auto fullscreen = frame.left <= monitor.left && frame.top <= monitor.top && frame.right >= monitor.right
	               && frame.bottom >= monitor.bottom;

	auto shown = IsWindowVisible(hwnd) && !IsIconic(hwnd) && !cloaked(hwnd) && !IsHungAppWindow(hwnd) && !fullscreen
	          && this->placed.bottom - this->placed.top >= MIN_HEADER;

	if (shown != this->mShown || active != this->mActive || title != this->mTitle || canMin != this->mCanMinimize
	    || canMax != this->mCanMaximize)
	{
		this->mShown = shown;
		this->mActive = active;
		this->mTitle = title;
		this->mCanMinimize = canMin;
		this->mCanMaximize = canMax;
		emit this->stateChanged();
		this->sampleColor();
	}

	constexpr int SAMPLE_EVERY = 2000 / POLL_MS;
	QSize size(frame.right - frame.left, frame.bottom - frame.top);
	if (shown && (size != this->sampledFor || (active && ++this->sampleTick >= SAMPLE_EVERY))) {
		this->sampledFor = size;
		this->sampleTick = 0;
		this->sampleColor();
	}
}

// keeps the header in the space above the app's frame, where Windows' title bar was
void CaptionSkin::place() {
	auto* hwnd = this->mAttached;
	auto* own = this->ownHwnd();
	if (hwnd == nullptr || own == nullptr || !IsWindow(hwnd) || IsIconic(hwnd) || this->drag != Drag::None) return;

	auto frame = frameOf(hwnd);
	auto height = this->headerPx();

	// the header never goes above the usable screen (under a bar / off the top): the window comes down instead,
	// the way Windows keeps a title bar where it can be grabbed
	auto work = monitorOf(hwnd).rcWork;
	if (frame.top - height < work.top && frame.bottom > work.top) {
		auto down = work.top - (frame.top - height);
		RECT window {};
		GetWindowRect(hwnd, &window);
		SetWindowPos(hwnd, nullptr, window.left, window.top + down, 0, 0, SWP_NOSIZE | ASYNC_MOVE);
		OffsetRect(&frame, 0, down);
	}

	auto corner = this->cornerPx();
	RECT header {frame.left, frame.top - height, frame.right, frame.top + corner};

	if (!EqualRect(&header, &this->placed)) {
		SetWindowPos(
		    own,
		    nullptr,
		    header.left,
		    header.top,
		    header.right - header.left,
		    header.bottom - header.top,
		    SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE
		);
		this->placed = header;
	}

	auto dpi = GetDpiForWindow(hwnd);
	auto dpr = (dpi == 0 ? 96 : dpi) / 96.0;
	auto logical = height / dpr;
	auto radius = corner / dpr;
	if (logical != this->mHeaderHeight || radius != this->mRadius) {
		this->mHeaderHeight = logical;
		this->mRadius = radius;
		emit this->layoutChanged();
	}
}

void CaptionSkin::activate() {
	if (this->mAttached == nullptr) return;
	if (IsIconic(this->mAttached)) ShowWindowAsync(this->mAttached, SW_RESTORE);
	qs::windows::forceForegroundWindow(this->mAttached);
}

void CaptionSkin::minimize() {
	if (this->mAttached != nullptr) PostMessageW(this->mAttached, WM_SYSCOMMAND, SC_MINIMIZE, 0);
}

void CaptionSkin::close() {
	if (this->mAttached != nullptr) PostMessageW(this->mAttached, WM_SYSCOMMAND, SC_CLOSE, 0);
}

void CaptionSkin::showMenu() {
	if (this->mAttached == nullptr) return;
	this->activate();
	POINT cursor {};
	GetCursorPos(&cursor);
	PostMessageW(this->mAttached, WM_POPUPSYSTEMMENU, 0, MAKELPARAM(cursor.x, cursor.y));
}

void CaptionSkin::toggleMaximize() {
	if (this->mAttached == nullptr || !this->mCanMaximize) return;
	if (!this->mMaximized) GetWindowRect(this->mAttached, &this->normal);
	this->maximizeTo(!this->mMaximized);
}

// maximised = the work area under the header (the header is where Windows' title bar was)
void CaptionSkin::maximizeTo(bool maximized) {
	auto* hwnd = this->mAttached;
	if (maximized) {
		RECT window {};
		GetWindowRect(hwnd, &window);
		auto frame = frameOf(hwnd);
		auto work = monitorOf(hwnd).rcWork;
		auto height = this->headerPx();
		// Windows' invisible resize borders around the visible frame
		RECT rect {
		    work.left - (frame.left - window.left),
		    work.top + height - (frame.top - window.top),
		    work.right + (window.right - frame.right),
		    work.bottom + (window.bottom - frame.bottom)
		};
		SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, ASYNC_MOVE);
	} else if (this->normal.right > this->normal.left) {
		auto& r = this->normal;
		SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, ASYNC_MOVE);
	}

	if (maximized != this->mMaximized) {
		this->mMaximized = maximized;
		emit this->stateChanged();
	}
}

void CaptionSkin::startMove() {
	if (this->mAttached == nullptr) return;
	GetCursorPos(&this->dragFrom);
	GetWindowRect(this->mAttached, &this->dragRect);
	this->armed = true;
	this->beginDrag(Drag::Move);
}

// the mouse moved far enough after a press on the title bar: the move really starts
void CaptionSkin::startMoving(POINT cursor) {
	auto* hwnd = this->mAttached;
	this->armed = false;

	// a maximised window goes back to its size first, kept under the mouse at the same spot across it
	if (this->mMaximized && this->normal.right > this->normal.left) {
		auto frame = frameOf(hwnd);
		auto fraction = static_cast<double>(cursor.x - frame.left) / qMax(1L, frame.right - frame.left);
		auto width = this->normal.right - this->normal.left;
		auto height = this->normal.bottom - this->normal.top;
		auto left = cursor.x - static_cast<LONG>(fraction * width);
		auto top = cursor.y + this->dragHeader / 2 - this->dragInsets.top;
		this->dragRect = {left, top, left + width, top + height};
		this->dragFrom = cursor;
		this->mMaximized = false;
		emit this->stateChanged();
	}
}

void CaptionSkin::startResize() {
	auto* hwnd = this->mAttached;
	if (hwnd == nullptr || !this->mCanMaximize || this->mMaximized) return;
	GetCursorPos(&this->dragFrom);
	GetWindowRect(hwnd, &this->dragRect);
	this->beginDrag(Drag::Resize);
}

void CaptionSkin::beginDrag(Drag kind) {
	RECT window {};
	GetWindowRect(this->mAttached, &window);
	auto frame = frameOf(this->mAttached);
	this->dragInsets = {frame.left - window.left, frame.top - window.top, window.right - frame.right, 0};
	this->dragHeader = this->headerPx();
	this->dragExtra = this->cornerPx();
	this->drag = kind;
	this->dragTimer.start();
}

void CaptionSkin::dragStep() {
	auto* hwnd = this->mAttached;
	auto* own = this->ownHwnd();
	auto button = GetSystemMetrics(SM_SWAPBUTTON) != 0 ? VK_RBUTTON : VK_LBUTTON;
	auto held = (GetAsyncKeyState(button) & 0x8000) != 0;
	POINT cursor {};
	GetCursorPos(&cursor);

	if (hwnd == nullptr || own == nullptr || !IsWindow(hwnd)) {
		this->dragTimer.stop();
		this->drag = Drag::None;
		return;
	}

	if (!held) {
		auto was = this->armed ? Drag::None : this->drag;
		this->armed = false;
		this->dragTimer.stop();
		this->drag = Drag::None;
		// let go at the top of the screen: maximise, like dragging a title bar there
		if (was == Drag::Move && this->mCanMaximize && cursor.y <= monitorAt(cursor).rcMonitor.top + 1) {
			this->normal = this->dragRect;
			auto dx = cursor.x - this->dragFrom.x;
			auto dy = cursor.y - this->dragFrom.y;
			OffsetRect(&this->normal, dx, dy);
			this->maximizeTo(true);
		}
		this->placed = {};
		this->place();
		return;
	}

	if (this->armed) {
		constexpr LONG SLOP = 4;
		if (qAbs(cursor.x - this->dragFrom.x) < SLOP && qAbs(cursor.y - this->dragFrom.y) < SLOP) return;
		this->startMoving(cursor);
	}

	auto dx = cursor.x - this->dragFrom.x;
	auto dy = cursor.y - this->dragFrom.y;
	RECT rect = this->dragRect;
	if (this->drag == Drag::Move) {
		OffsetRect(&rect, dx, dy);
	} else {
		rect.top = qMin(this->dragRect.top + dy, this->dragRect.bottom - MIN_HEIGHT);
	}
	SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, ASYNC_MOVE);

	// the header goes with it right away (not after the app has moved), from the rect the window is moving to
	auto& in = this->dragInsets;
	SetWindowPos(
	    own,
	    nullptr,
	    rect.left + in.left,
	    rect.top + in.top - this->dragHeader,
	    (rect.right - in.right) - (rect.left + in.left),
	    this->dragHeader + this->dragExtra,
	    SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE
	);
}

} // namespace qs::windows::sys
