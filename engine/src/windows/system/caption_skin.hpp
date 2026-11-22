#pragma once

#include <qt_windows.h>

#include <qcolor.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qrect.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qwindow.h>

namespace qs::windows::sys {

/// The title bar of another app's window, drawn by the shell.
///
/// With WindowManager.titleBars "skin", Windows' title bar is removed from the app (WindowManager.captionTargets
/// lists those windows) and the window's top edge is lowered by its height, so the app's content stays where it was.
/// `window` (a shell window) is then kept in that space, right on top of the app's frame: it is the app's title bar.
/// It is an owned tool window of the app, so it stays with it, hides with it and has no taskbar / Alt+Tab entry.
///
/// The header handles the title bar's job through the functions below: drag to move (to the top of the screen to
/// maximise), double-click to maximise, the min / max / close buttons, right-click for the window menu, and the top
/// edge to resize. A maximised window fills the work area under its header (Windows' own maximise would put the app
/// over the header's space, so it is turned into this one).
class CaptionSkin: public QObject {
	Q_OBJECT;
	QML_ELEMENT;

	Q_PROPERTY(QObject* window READ window WRITE setWindow NOTIFY windowChanged);
	Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY targetChanged);
	/// whether the header should be on screen (the app is visible, on this desktop, not minimised or fullscreen)
	Q_PROPERTY(bool shown READ shown NOTIFY stateChanged);
	Q_PROPERTY(bool active READ active NOTIFY stateChanged);
	Q_PROPERTY(bool maximized READ maximized NOTIFY stateChanged);
	Q_PROPERTY(bool canMinimize READ canMinimize NOTIFY stateChanged);
	Q_PROPERTY(bool canMaximize READ canMaximize NOTIFY stateChanged);
	Q_PROPERTY(QString title READ title NOTIFY stateChanged);
	Q_PROPERTY(QString exePath READ exePath NOTIFY stateChanged);
	/// the header's height in logical px (the height Windows' title bar had)
	Q_PROPERTY(qreal headerHeight READ headerHeight NOTIFY layoutChanged);
	/// radius of the window's corners in logical px (0 when maximised or not rounded). The shell window is this much
	/// taller than the header: that strip overlaps the app's top corners, where the header fills the gaps the
	/// rounding leaves (in appColor), so header + app are one rounded window.
	Q_PROPERTY(qreal cornerRadius READ cornerRadius NOTIFY layoutChanged);
	/// the colour along the top of the app's content (sampled from the screen), for headers that blend in
	Q_PROPERTY(QColor appColor READ appColor NOTIFY appColorChanged);
	/// the app window's outline colour (Windows 11), so it matches the header; invalid = Windows' own
	Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY borderColorChanged);

public:
	explicit CaptionSkin(QObject* parent = nullptr);
	~CaptionSkin() override;
	Q_DISABLE_COPY_MOVE(CaptionSkin);

	[[nodiscard]] QObject* window() const { return this->mWindow.data(); }
	void setWindow(QObject* window);
	[[nodiscard]] QString target() const { return this->mTarget; }
	void setTarget(const QString& target);

	[[nodiscard]] bool shown() const { return this->mShown; }
	[[nodiscard]] bool active() const { return this->mActive; }
	[[nodiscard]] bool maximized() const { return this->mMaximized; }
	[[nodiscard]] bool canMinimize() const { return this->mCanMinimize; }
	[[nodiscard]] bool canMaximize() const { return this->mCanMaximize; }
	[[nodiscard]] QString title() const { return this->mTitle; }
	[[nodiscard]] QString exePath() const { return this->mExePath; }
	[[nodiscard]] qreal headerHeight() const { return this->mHeaderHeight; }
	[[nodiscard]] qreal cornerRadius() const { return this->mRadius; }
	[[nodiscard]] QColor appColor() const { return this->mAppColor; }
	[[nodiscard]] QColor borderColor() const { return this->mBorderColor; }
	void setBorderColor(const QColor& color);

	/// brings the app to the front and gives it the keyboard (a click on its title bar)
	Q_INVOKABLE void activate();
	/// starts moving the app with the mouse until the button is let go (press on the title bar)
	Q_INVOKABLE void startMove();
	/// starts resizing the app's top edge with the mouse (press on the header's top edge)
	Q_INVOKABLE void startResize();
	Q_INVOKABLE void minimize();
	Q_INVOKABLE void toggleMaximize();
	Q_INVOKABLE void close();
	/// the app's window menu (Restore, Move, Size, Minimise, Maximise, Close) at the mouse
	Q_INVOKABLE void showMenu();

	void place();
	void poll();

signals:
	void windowChanged();
	void targetChanged();
	void stateChanged();
	void layoutChanged();
	void appColorChanged();
	void borderColorChanged();

private:
	enum class Drag : quint8 { None, Move, Resize };

	void attach();
	void detach();
	[[nodiscard]] HWND ownHwnd() const;
	[[nodiscard]] int headerPx() const;
	[[nodiscard]] int cornerPx() const;
	void sampleColor();
	void applyBorder();
	void beginDrag(Drag kind);
	void startMoving(POINT cursor);
	void dragStep();
	void maximizeTo(bool maximized);

	QPointer<QWindow> mWindow;
	QString mTarget;
	HWND mHwnd = nullptr;
	HWND mAttached = nullptr;
	QTimer timer;
	QTimer dragTimer;

	bool mShown = false;
	bool mActive = false;
	bool mMaximized = false;
	bool mCanMinimize = true;
	bool mCanMaximize = true;
	QString mTitle;
	QString mExePath;
	qreal mHeaderHeight = 0;
	qreal mRadius = 0;
	QColor mAppColor;
	QColor mBorderColor;
	bool borderSet = false;
	int sampleTick = 0;
	QSize sampledFor;

	RECT placed {};
	RECT normal {}; // the window's rect before our maximise
	Drag drag = Drag::None;
	POINT dragFrom {};
	RECT dragRect {};
	RECT dragInsets {}; // visible frame inside the window rect (Windows' invisible borders)
	int dragHeader = 0;
	int dragExtra = 0;
	bool armed = false; // pressed, not moved yet: a click or double-click is not a move
};

} // namespace qs::windows::sys
