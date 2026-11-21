#pragma once

#include <qt_windows.h>
#include <dwmapi.h>

#include <qhash.h>
#include <qlist.h>
#include <qrect.h>

namespace qs::windows {

// Hyprland-style window animations that never touch the apps while they run.
//
// When the tiler moves windows, a click-through overlay is put over the whole desktop. It shows a live DWM thumbnail
// of every visible window, stacked like the real ones. The real windows are then moved/resized ONCE, straight to where
// they end up, hidden underneath; only their thumbnails glide (and stretch) from the old place to the new one. When
// the animation ends the overlay goes away and the real windows are already in place.
//
// Apps never see in-between sizes, so terminals, Electron (Discord, Spotify) and games can't be garbled by being
// resized every frame, and a crash mid-animation leaves nothing wrong: the overlay dies with the shell.
class ThumbOverlay {
public:
	ThumbOverlay() = default;
	~ThumbOverlay();
	ThumbOverlay(const ThumbOverlay&) = delete;
	ThumbOverlay& operator=(const ThumbOverlay&) = delete;

	// A window will glide from `from` to `to` (frame rectangles, physical pixels). Call before the real window is
	// moved: the first call of an animation snapshots the whole desktop. Returns false if the overlay can't be used
	// (the caller then just places the window).
	bool add(HWND hwnd, const QRect& from, const QRect& to);

	// progress 0..1 already eased (it may overshoot past 1 with an overshooting bezier)
	void tick(double eased);

	// remove the overlay; the real windows show again
	void finish();

	[[nodiscard]] bool active() const { return this->shown; }

private:
	struct Thumb {
		HTHUMBNAIL handle = nullptr;
		QRect from;   // frame rect on screen
		QRect to;
		QRect source; // frame inside the window (crops the invisible resize borders)
		bool moving = false;
	};

	bool ensureWindow();
	void snapshot();
	bool registerThumb(HWND hwnd, Thumb& thumb);
	void place(const Thumb& thumb, const QRect& rect) const;

	HWND overlay = nullptr;
	QRect desktop;   // virtual screen, physical pixels
	bool shown = false;
	QList<HWND> order;   // bottom to top
	QHash<HWND, Thumb> thumbs;
};

} // namespace qs::windows
