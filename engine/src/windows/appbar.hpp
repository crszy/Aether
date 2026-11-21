#pragma once

#include <qt_windows.h>

#include <qrect.h>
#include <qtclasshelpermacros.h>
#include <qtypes.h>

namespace qs::windows {

class WinAppBar {
public:
	WinAppBar() = default;
	~WinAppBar() { this->remove(); }
	Q_DISABLE_COPY_MOVE(WinAppBar);

	static UINT callbackMessage();
	static UINT taskbarCreatedMessage();
	static void removeStale();

	[[nodiscard]] bool registered() const { return this->mRegistered; }
	[[nodiscard]] HWND hwnd() const { return this->mHwnd; }
	[[nodiscard]] QRect reservedRect() const { return this->mReserved; }

	QRect reserve(HWND hwnd, UINT edge, const QRect& monitor, qint32 size);

	void remove();

	void invalidate();
	void invalidatePosition();

	void adopt(WinAppBar& other);

	void notifyActivate();
	void notifyWindowPosChanged();

private:
	bool ensureRegistered(HWND hwnd);

	HWND mHwnd = nullptr;
	HWND mFallback = nullptr;      // reserved without an AppBar server (see applyWorkArea in appbar.cpp)
	QRect mFallbackMonitor;
	bool mRegistered = false;
	UINT mEdge = 0;
	QRect mReserved;
	QRect mMonitor;
	qint32 mSize = 0;
	bool mPositionValid = false;
};

} // namespace qs::windows
