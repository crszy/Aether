#include "window_decorations.hpp"
#include <array>
#include <atomic>
#include <cstddef>

#include <qcolor.h>
#include <qcoreapplication.h>
#include <qhash.h>
#include <qobject.h>
#include <qpointer.h>
#include <qset.h>
#include <qstring.h>
#include <qtimer.h>

#include "tiling.hpp"
#include "util.hpp"
#include "window_tracker.hpp"

#include <dwmapi.h>

namespace qs::windows {

namespace {

constexpr DWORD WINDOWS_11_BUILD = 22000;
constexpr std::size_t CRASH_SLOTS = 512;

std::array<std::atomic<HWND>, CRASH_SLOTS> gSlots {}; // NOLINT
std::atomic<bool> gBordersOverridden = false;          // NOLINT
std::atomic<bool> gCornersOverridden = false;          // NOLINT

void rememberForCrash(HWND hwnd) {
	std::atomic<HWND>* free = nullptr;

	for (auto& slot: gSlots) {
		auto* value = slot.load();
		if (value == hwnd) return;
		if (value == nullptr && free == nullptr) free = &slot;
	}

	if (free != nullptr) free->store(hwnd);
}

void forgetForCrash(HWND hwnd) {
	for (auto& slot: gSlots) {
		if (slot.load() == hwnd) slot.store(nullptr);
	}
}

COLORREF colorRef(const QColor& color) {
	auto rgb = color.toRgb();
	return RGB(rgb.red(), rgb.green(), rgb.blue());
}

bool setBorder(HWND hwnd, COLORREF color) {
	return SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &color, sizeof(color)));
}

bool setCorner(HWND hwnd, DWORD preference) {
	auto value = static_cast<DWM_WINDOW_CORNER_PREFERENCE>(preference);
	return SUCCEEDED(
	    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &value, sizeof(value))
	);
}

bool reachable(HWND hwnd) { return IsWindow(hwnd) && !IsHungAppWindow(hwnd); }

// Windows whose title bar was stripped, so a crash can put it back (kept apart from the border slots: the
// crash path adds WS_CAPTION, which must only go to windows that had it)
std::array<std::atomic<HWND>, CRASH_SLOTS> gStripSlots {}; // NOLINT
std::atomic<bool> gStripping = false;                      // NOLINT
std::atomic<bool> gTheming = false;                        // NOLINT
std::atomic<int> gCaptionHeight = 0;                       // NOLINT

// a flat, recoloured title bar (Windows 11 lets another process set these; apps that draw their own ignore them)
void setCaptionColors(HWND hwnd, COLORREF caption, COLORREF text) {
	DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
	DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
}

void rememberStripped(HWND hwnd) {
	std::atomic<HWND>* free = nullptr;
	for (auto& slot: gStripSlots) {
		auto* value = slot.load();
		if (value == hwnd) return;
		if (value == nullptr && free == nullptr) free = &slot;
	}
	if (free != nullptr) free->store(hwnd);
}

void forgetStripped(HWND hwnd) {
	for (auto& slot: gStripSlots) {
		if (slot.load() == hwnd) slot.store(nullptr);
	}
}

void refreshFrame(HWND hwnd) {
	SetWindowPos(
	    hwnd,
	    nullptr,
	    0,
	    0,
	    0,
	    0,
	    SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE
	        | SWP_ASYNCWINDOWPOS
	);
}

// removes the caption (title bar) and keeps the resize frame, so edges still resize the window
bool stripCaption(HWND hwnd) {
	auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
	if ((style & WS_CAPTION) != WS_CAPTION) return false;
	SetLastError(0);
	if (SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~static_cast<LONG_PTR>(WS_CAPTION)) == 0
	    && GetLastError() != 0)
	{
		return false;
	}
	// the window manager (komorebi fork) only tiles windows with a caption: this mark tells it the window is still
	// a normal app window, or it was never tiled again after a minimize (2026-11-15)
	SetPropW(hwnd, L"AetherCaptionStripped", reinterpret_cast<HANDLE>(1)); // NOLINT
	refreshFrame(hwnd);
	return true;
}

// the height Windows' title bar takes on this window (what removing WS_CAPTION gives back to the content)
int captionBarHeight(HWND hwnd) {
	auto style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
	auto ex = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
	auto dpi = GetDpiForWindow(hwnd);
	if (dpi == 0) dpi = 96;
	auto menu = GetMenu(hwnd) != nullptr;
	RECT with {};
	RECT without {};
	AdjustWindowRectExForDpi(&with, style | WS_CAPTION, menu, ex, dpi);
	AdjustWindowRectExForDpi(&without, style & ~WS_CAPTION, menu, ex, dpi);
	return without.top - with.top;
}

// skin: removes the title bar and lowers the window's top edge by its height in the same step, so the app's content
// does not move: the space the title bar had is where the shell's header goes. Returns the shift (0 when the
// window is maximised / minimised and keeps its place), or -1 when the title bar could not be removed.
// kept on the app window itself (shift + 1): a shell started later (after a stop / crash that skipped the clean-up) knows
// the window is one of ours and by how much its top edge was lowered
constexpr const wchar_t* SHIFT_PROP = L"AetherCaptionShift";

int shiftMark(HWND hwnd) { return static_cast<int>(reinterpret_cast<intptr_t>(GetPropW(hwnd, SHIFT_PROP))); }

int stripKeepingContent(HWND hwnd) {
	auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
	auto hasCaption = (style & WS_CAPTION) == WS_CAPTION;
	// already ours from an earlier shell: take it back as it is (its top edge is still lowered)
	if (auto mark = shiftMark(hwnd); mark > 0) {
		if (hasCaption) {
			SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~static_cast<LONG_PTR>(WS_CAPTION));
			refreshFrame(hwnd);
		}
		return mark - 1;
	}
	if (!hasCaption) return -1;
	auto shift = DecorationManager::headerPx(hwnd);
	RECT rect {};
	auto movable = !IsZoomed(hwnd) && !IsIconic(hwnd) && GetWindowRect(hwnd, &rect) && shift > 0;
	SetLastError(0);
	if (SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~static_cast<LONG_PTR>(WS_CAPTION)) == 0 && GetLastError() != 0)
		return -1;
	SetPropW(hwnd, L"AetherCaptionStripped", reinterpret_cast<HANDLE>(1)); // NOLINT (see stripCaption)
	constexpr UINT FLAGS = SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS;
	if (movable) {
		SetWindowPos(hwnd, nullptr, rect.left, rect.top + shift, rect.right - rect.left, rect.bottom - rect.top - shift, FLAGS);
		SetPropW(hwnd, SHIFT_PROP, reinterpret_cast<HANDLE>(static_cast<intptr_t>(shift + 1))); // NOLINT
		return shift;
	}
	refreshFrame(hwnd);
	SetPropW(hwnd, SHIFT_PROP, reinterpret_cast<HANDLE>(static_cast<intptr_t>(1))); // NOLINT
	return 0;
}

// puts the title bar back and raises the top edge again by what stripKeepingContent lowered it
void restoreCaptionShift(HWND hwnd, int shift) {
	RemovePropW(hwnd, SHIFT_PROP);
	RemovePropW(hwnd, L"AetherCaptionStripped");
	auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
	if ((style & WS_CAPTION) != WS_CAPTION) SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_CAPTION);
	RECT rect {};
	constexpr UINT FLAGS = SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS;
	if (shift > 0 && !IsZoomed(hwnd) && !IsIconic(hwnd) && GetWindowRect(hwnd, &rect)) {
		SetWindowPos(hwnd, nullptr, rect.left, rect.top - shift, rect.right - rect.left, rect.bottom - rect.top + shift, FLAGS);
	} else {
		refreshFrame(hwnd);
	}
}

void restoreCaption(HWND hwnd) {
	RemovePropW(hwnd, L"AetherCaptionStripped");
	auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
	if ((style & WS_CAPTION) == WS_CAPTION) return;
	SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_CAPTION);
	refreshFrame(hwnd);
}

// a stripped window keeps its resize frame, whose top edge Windows draws as a light line in light mode; a dark
// frame makes it a faint dark edge. Returns true when it turned dark mode on (so restoring turns it off again).
bool darkenFrame(HWND hwnd) {
	BOOL dark = FALSE;
	if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark))) && dark)
		return false;
	dark = TRUE;
	return SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)));
}

void undarkenFrame(HWND hwnd) {
	BOOL dark = FALSE;
	DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

// true when the window draws its own title bar (Chrome, Edge, Discord, VS Code, Terminal, Explorer...): its
// client area starts at the top of the window, so there is no system title bar to take away
bool drawsOwnTitleBar(HWND hwnd) {
	RECT window {};
	if (!GetWindowRect(hwnd, &window)) return true;
	POINT origin {0, 0};
	if (!ClientToScreen(hwnd, &origin)) return true;
	auto dpi = GetDpiForWindow(hwnd);
	auto caption = GetSystemMetricsForDpi(SM_CYCAPTION, dpi == 0 ? 96 : dpi);
	return origin.y - window.top < caption / 2;
}

} // namespace

DecorationManager* DecorationManager::instance() {
	static QPointer<DecorationManager> manager; // NOLINT

	if (manager.isNull()) {
		manager = new DecorationManager(QCoreApplication::instance());
	}

	return manager.data();
}

DecorationManager::DecorationManager(QObject* parent): QObject(parent) {
	QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
		this->restoreAll();
		this->stop();
	});
}

DecorationManager::~DecorationManager() { this->restoreAll(); }

bool DecorationManager::supported() { return windowsBuild() >= WINDOWS_11_BUILD; }

void DecorationManager::restoreForCrash() {
	if (gStripping.load()) {
		for (auto& slot: gStripSlots) {
			auto* hwnd = slot.load();
			if (hwnd != nullptr && reachable(hwnd)) restoreCaption(hwnd);
		}
	}

	auto borders = gBordersOverridden.load();
	auto corners = gCornersOverridden.load();
	auto themed = gTheming.load();
	if (!borders && !corners && !themed) return;

	for (auto& slot: gSlots) {
		auto* hwnd = slot.load();
		if (hwnd == nullptr || !reachable(hwnd)) continue;
		if (borders) setBorder(hwnd, DWMWA_COLOR_DEFAULT);
		if (corners) setCorner(hwnd, DWMWCP_DEFAULT);
		if (themed) setCaptionColors(hwnd, DWMWA_COLOR_DEFAULT, DWMWA_COLOR_DEFAULT);
	}
}

void DecorationManager::setBorders(const QString& borders) {
	auto value = borders.trimmed().toLower();
	if (value != QStringLiteral("colors") && value != QStringLiteral("none")) {
		value = QStringLiteral("system");
	}

	if (value == this->mBorders) return;
	this->mBorders = value;
	this->update();
	emit this->bordersChanged();
}

void DecorationManager::setActiveColor(const QColor& color) {
	if (color == this->mActiveColor) return;
	this->mActiveColor = color;
	if (this->borderMode() == BorderMode::Colors) this->update();
	emit this->activeColorChanged();
}

void DecorationManager::setInactiveColor(const QColor& color) {
	if (color == this->mInactiveColor) return;
	this->mInactiveColor = color;
	if (this->borderMode() == BorderMode::Colors) this->update();
	emit this->inactiveColorChanged();
}

void DecorationManager::setCorners(const QString& corners) {
	auto value = corners.trimmed();
	if (value != QStringLiteral("round") && value != QStringLiteral("roundSmall")
	    && value != QStringLiteral("square"))
	{
		value = QStringLiteral("default");
	}

	if (value == this->mCorners) return;
	this->mCorners = value;
	this->update();
	emit this->cornersChanged();
}

void DecorationManager::setTitleBars(const QString& titleBars) {
	auto value = titleBars.trimmed().toLower();
	if (value != QStringLiteral("strip") && value != QStringLiteral("themed") && value != QStringLiteral("skin"))
		value = QStringLiteral("system");

	if (value == this->mTitleBars) return;
	this->mTitleBars = value;
	this->update();
	emit this->titleBarsChanged();
}

void DecorationManager::setTitleBarColor(const QColor& color) {
	if (color == this->mTitleBarColor) return;
	this->mTitleBarColor = color;
	if (this->theming()) this->update();
	emit this->titleBarColorChanged();
}

void DecorationManager::setTitleBarTextColor(const QColor& color) {
	if (color == this->mTitleBarTextColor) return;
	this->mTitleBarTextColor = color;
	if (this->theming()) this->update();
	emit this->titleBarTextColorChanged();
}

bool DecorationManager::theming() const {
	// (skin: the header colours the app's frame itself, CaptionSkin.borderColor)
	return this->mTitleBars == QStringLiteral("themed") && this->mTitleBarColor.isValid() && supported();
}

int DecorationManager::headerPx(HWND hwnd) {
	auto dpi = GetDpiForWindow(hwnd);
	if (dpi == 0) dpi = 96;
	return qMax(captionBarHeight(hwnd), MulDiv(gCaptionHeight.load(), static_cast<int>(dpi), 96));
}

void DecorationManager::setCaptionHeight(int height) {
	height = qBound(0, height, 96);
	if (height == this->mCaptionHeight) return;
	this->mCaptionHeight = height;
	gCaptionHeight.store(height);
	// windows already wearing a header are lowered by the old height: put them back and do them again
	if (this->skinning() && this->running) {
		this->restoreAll();
		this->applyAll();
	}
	emit this->captionHeightChanged();
}

bool DecorationManager::skinning() const { return this->mTitleBars == QStringLiteral("skin"); }

// a new window is judged only after it has been up a moment: Firefox-based browsers (Zen...) open with a normal
// title bar and swap in their own drawn one right after, and must not be caught in between
bool DecorationManager::settled(HWND hwnd) {
	// only Gecko windows swap their frame after opening: every other app is stripped at once (the 1.5 s wait for
	// all of them showed each new window's title bar before it went - 2026-11-15)
	wchar_t cls[64] {};
	GetClassNameW(hwnd, cls, 64);
	if (wcscmp(cls, L"MozillaWindowClass") != 0) return true;
	constexpr qint64 SETTLE_MS = 1500;
	auto now = QDateTime::currentMSecsSinceEpoch();
	auto iter = this->firstSeen.find(hwnd);
	if (iter == this->firstSeen.end()) iter = this->firstSeen.insert(hwnd, now);
	auto age = now - iter.value();
	if (age >= SETTLE_MS) return true;
	QTimer::singleShot(SETTLE_MS - age + 50, this, [this, hwnd]() {
		if (this->tracker == nullptr) return;
		if (auto* window = this->tracker->windowFor(hwnd)) this->apply(window);
	});
	return false;
}

void DecorationManager::publishTargets() {
	QStringList targets;
	for (auto iter = this->applied.cbegin(); iter != this->applied.cend(); ++iter) {
		if (iter.value().skinned) targets.append(QString::number(reinterpret_cast<quintptr>(iter.key())));
	}
	targets.sort();
	if (targets == this->mCaptionTargets) return;
	this->mCaptionTargets = targets;
	emit this->captionTargetsChanged();
}

void DecorationManager::setTitleBarKeep(const QStringList& keep) {
	QStringList value;
	for (const auto& name: keep) {
		auto trimmed = name.trimmed().toLower();
		if (!trimmed.isEmpty()) value.append(trimmed);
	}

	if (value == this->mTitleBarKeep) return;
	this->mTitleBarKeep = value;
	if (this->stripping() || this->theming() || this->skinning()) this->update();
	emit this->titleBarKeepChanged();
}

bool DecorationManager::stripping() const { return this->mTitleBars == QStringLiteral("strip"); }

// whether this window's title bar is ours to change (strip or recolour): keep list, then main windows only
bool DecorationManager::shouldStrip(TrackedWindow* window) const {
	if (!this->stripping() && !this->theming() && !this->skinning()) return false;

	auto exe = window->exePath();
	auto slash = qMax(exe.lastIndexOf(u'\\'), exe.lastIndexOf(u'/'));
	auto name = exe.mid(slash + 1).toLower();
	if (this->mTitleBarKeep.contains(name)) return false;
	// browsers that draw their own title bar (tabs in it), even when they open with Windows' one for a moment
	static const QStringList OWN_TITLE_BAR = {
	    QStringLiteral("zen.exe"), QStringLiteral("firefox.exe"), QStringLiteral("librewolf.exe"),
	    QStringLiteral("floorp.exe"), QStringLiteral("waterfox.exe"), QStringLiteral("thunderbird.exe"),
	    QStringLiteral("mullvadbrowser.exe"), QStringLiteral("tor.exe")
	};
	if (OWN_TITLE_BAR.contains(name)) return false;

	auto* hwnd = window->hwnd();
	auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
	// only resizable main windows: never dialogs, owned windows or fixed-size popups
	if ((style & WS_CHILD) != 0 || (style & WS_THICKFRAME) == 0) return false;
	if (GetWindow(hwnd, GW_OWNER) != nullptr) return false;
	if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_DLGMODALFRAME) != 0) return false;
	return true;
}

DecorationManager::BorderMode DecorationManager::borderMode() const {
	if (this->mBorders == QStringLiteral("colors")) return BorderMode::Colors;
	if (this->mBorders == QStringLiteral("none")) return BorderMode::None;
	return BorderMode::System;
}

DWORD DecorationManager::cornerPreference() const {
	if (this->mCorners == QStringLiteral("round")) return DWMWCP_ROUND;
	if (this->mCorners == QStringLiteral("roundSmall")) return DWMWCP_ROUNDSMALL;
	if (this->mCorners == QStringLiteral("square")) return DWMWCP_DONOTROUND;
	return DWMWCP_DEFAULT;
}

bool DecorationManager::overriding() const {
	if (this->stripping() || this->theming() || this->skinning()) return true;
	if (!DecorationManager::supported()) return false;
	return this->borderMode() != BorderMode::System || this->cornerPreference() != DWMWCP_DEFAULT;
}

void DecorationManager::restoreState(HWND hwnd, const Applied& state) {
	if (reachable(hwnd)) {
		if (state.border) setBorder(hwnd, DWMWA_COLOR_DEFAULT);
		if (state.corner) setCorner(hwnd, DWMWCP_DEFAULT);
		if (state.darkened) undarkenFrame(hwnd);
		if (state.skinned) restoreCaptionShift(hwnd, state.shift);
		else if (state.stripped) restoreCaption(hwnd);
		if (state.themed) setCaptionColors(hwnd, DWMWA_COLOR_DEFAULT, DWMWA_COLOR_DEFAULT);
	}
	forgetForCrash(hwnd);
	forgetStripped(hwnd);
}

void DecorationManager::update() {
	auto supported = DecorationManager::supported();
	auto borders = supported && this->borderMode() != BorderMode::System;
	auto corners = supported && this->cornerPreference() != DWMWCP_DEFAULT;

	if (borders) gBordersOverridden.store(true);
	if (corners) gCornersOverridden.store(true);
	if (this->stripping()) gStripping.store(true);
	if (this->theming()) gTheming.store(true);

	if (this->overriding()) {
		if (!this->running) this->start();
		this->applyAll();
	} else if (this->running) {
		this->applyAll();
		this->stop();
	}

	gBordersOverridden.store(borders);
	gCornersOverridden.store(corners);
	gStripping.store(this->stripping());
	gTheming.store(this->theming());
}

void DecorationManager::start() {
	this->tracker = WindowTracker::instance();
	this->running = true;

	auto& c = this->connections;
	c.append(
	    QObject::connect(this->tracker, &WindowTracker::windowAdded, this, &DecorationManager::apply)
	);
	c.append(
	    QObject::connect(this->tracker, &WindowTracker::windowRemoved, this, &DecorationManager::forget)
	);
	c.append(QObject::connect(
	    this->tracker,
	    &WindowTracker::activeWindowChanged,
	    this,
	    &DecorationManager::onActiveWindowChanged
	));

	auto* active = this->tracker->activeWindow();
	this->lastActive = active == nullptr ? nullptr : active->hwnd();
}

void DecorationManager::stop() {
	for (const auto& connection: this->connections) QObject::disconnect(connection);
	this->connections.clear();
	this->restoreAll();
	this->lastActive = nullptr;
	this->running = false;
}

void DecorationManager::applyAll() {
	if (this->tracker == nullptr) return;

	QSet<HWND> tracked;
	for (auto* window: this->tracker->windows()) {
		tracked.insert(window->hwnd());
		this->apply(window);
	}

	for (auto iter = this->applied.begin(); iter != this->applied.end();) {
		if (tracked.contains(iter.key())) {
			++iter;
			continue;
		}

		DecorationManager::restoreState(iter.key(), iter.value());
		iter = this->applied.erase(iter);
	}
	this->publishTargets();
}

void DecorationManager::apply(TrackedWindow* window) {
	if (window == nullptr || this->tracker == nullptr) return;

	auto* hwnd = window->hwnd();
	if (isOwnProcessWindow(hwnd) || !reachable(hwnd)) return;

	auto state = this->applied.value(hwnd);

	auto mode = this->borderMode();
	auto color = window == this->tracker->activeWindow() ? this->mActiveColor : this->mInactiveColor;
	if (mode == BorderMode::Colors && !color.isValid()) mode = BorderMode::System;

	if (state.skinned) {
		// the window's outline is set by its header (CaptionSkin.borderColor), so header and app read as one
	} else if (mode != BorderMode::System) {
		auto value = mode == BorderMode::None ? DWMWA_COLOR_NONE : colorRef(color);
		if ((!state.border || state.color != value) && setBorder(hwnd, value)) {
			state.border = true;
			state.color = value;
		}
	} else if (state.border) {
		setBorder(hwnd, DWMWA_COLOR_DEFAULT);
		state.border = false;
	}

	auto preference = this->cornerPreference();
	if (preference != DWMWCP_DEFAULT) {
		if ((!state.corner || state.preference != preference) && setCorner(hwnd, preference)) {
			state.corner = true;
			state.preference = preference;
		}
	} else if (state.corner) {
		setCorner(hwnd, DWMWCP_DEFAULT);
		state.corner = false;
	}

	// title bar: stripped only once, and only when Windows draws it (not apps that draw their own)
	auto target = this->shouldStrip(window);

	// themed: the title bar stays (drag, snap, buttons) but in the shell's colours; the inactive window's
	// title is dimmed
	if (target && this->theming()) {
		auto caption = colorRef(this->mTitleBarColor);
		auto textColor = this->mTitleBarTextColor.isValid() ? this->mTitleBarTextColor : QColor(Qt::white);
		if (window != this->tracker->activeWindow()) {
			auto bg = this->mTitleBarColor.toRgb();
			auto fg = textColor.toRgb();
			textColor = QColor(
			    (fg.red() + bg.red()) / 2,
			    (fg.green() + bg.green()) / 2,
			    (fg.blue() + bg.blue()) / 2
			);
		}
		auto text = colorRef(textColor);
		if (!state.themed || state.caption != caption || state.text != text) {
			setCaptionColors(hwnd, caption, text);
			state.themed = true;
			state.caption = caption;
			state.text = text;
		}
	} else if (state.themed) {
		setCaptionColors(hwnd, DWMWA_COLOR_DEFAULT, DWMWA_COLOR_DEFAULT);
		state.themed = false;
	}

	// skin: Windows' title bar goes and the shell's header (CaptionSkin) becomes the window's top bar. Tiled
	// windows just lose the title bar, like on Hyprland (their tile has no room above them for a header).
	auto* tiling = TilingManager::instance();
	auto tiled = tiling != nullptr && tiling->enabled() && tiling->isTiled(window);
	if (target && this->skinning() && !tiled) {
		if (!state.skinned && !state.stripped && settled(hwnd) && (shiftMark(hwnd) > 0 || !drawsOwnTitleBar(hwnd))) {
			auto shift = stripKeepingContent(hwnd);
			if (shift >= 0) {
				state.skinned = true;
				state.stripped = true;
				state.shift = shift;
				rememberStripped(hwnd);
			}
		}
	} else if (state.skinned) {
		restoreCaptionShift(hwnd, state.shift);
		forgetStripped(hwnd);
		state.skinned = false;
		state.stripped = false;
		state.shift = 0;
		if (state.corner && this->cornerPreference() == DWMWCP_DEFAULT) {
			setCorner(hwnd, DWMWCP_DEFAULT);
			state.corner = false;
		}
	}

	if (state.skinned) {
		// nothing more: the strip below is for "strip" mode and tiled windows
	} else if (target && (this->stripping() || (this->skinning() && tiled))) {
		if (!state.stripped && settled(hwnd) && !drawsOwnTitleBar(hwnd)) {
			auto darkened = darkenFrame(hwnd);
			if (stripCaption(hwnd)) {
				state.stripped = true;
				state.darkened = darkened;
				rememberStripped(hwnd);
			} else if (darkened) {
				undarkenFrame(hwnd);
			}
		}
	} else if (state.stripped) {
		if (state.darkened) undarkenFrame(hwnd);
		restoreCaption(hwnd);
		forgetStripped(hwnd);
		state.stripped = false;
		state.darkened = false;
	}

	auto wasSkinned = this->applied.value(hwnd).skinned;
	if (state.border || state.corner || state.stripped || state.themed || state.skinned) {
		this->applied.insert(hwnd, state);
		if (state.border || state.corner || state.themed) rememberForCrash(hwnd);
		else forgetForCrash(hwnd);
	} else {
		this->applied.remove(hwnd);
		forgetForCrash(hwnd);
	}
	if (wasSkinned != state.skinned) this->publishTargets();
}

void DecorationManager::forget(TrackedWindow* window) {
	if (window == nullptr) return;

	auto* hwnd = window->hwnd();
	if (hwnd == this->lastActive) this->lastActive = nullptr;
	this->firstSeen.remove(hwnd);

	auto iter = this->applied.find(hwnd);
	if (iter == this->applied.end()) return;

	auto skinned = iter.value().skinned;
	DecorationManager::restoreState(hwnd, iter.value());
	this->applied.erase(iter);
	if (skinned) this->publishTargets();
}

void DecorationManager::restoreAll() {
	for (auto iter = this->applied.cbegin(); iter != this->applied.cend(); ++iter) {
		DecorationManager::restoreState(iter.key(), iter.value());
	}

	this->applied.clear();
	this->publishTargets();
}

void DecorationManager::onActiveWindowChanged() {
	auto* active = this->tracker->activeWindow();
	auto* hwnd = active == nullptr ? nullptr : active->hwnd();
	if (hwnd == this->lastActive) return;

	if (auto* previous = this->tracker->windowFor(this->lastActive)) this->apply(previous);
	this->lastActive = hwnd;
	if (active != nullptr) this->apply(active);
}

} // namespace qs::windows
