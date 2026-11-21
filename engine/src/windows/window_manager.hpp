#pragma once

#include <qt_windows.h>

#include <qcolor.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::windows {

class TrackedWindow;
class WindowTracker;
class VirtualDesktops;

class WorkspaceBehavior: public QObject {
	Q_OBJECT;

public:
	static WorkspaceBehavior* instance();
	Q_DISABLE_COPY_MOVE(WorkspaceBehavior);

	[[nodiscard]] bool backAndForth() const { return this->mBackAndForth; }
	void setBackAndForth(bool enabled);
	[[nodiscard]] qint32 warpOnChange() const { return this->mWarpOnChange; }
	void setWarpOnChange(qint32 mode);
	// Hyprland's input:follow_mouse: 0 off, 1 focus follows the mouse, 2 and 3 = Windows' click-to-focus
	[[nodiscard]] qint32 followMouse() const { return this->mFollowMouse; }
	void setFollowMouse(qint32 mode);

	[[nodiscard]] qsizetype previousDesktop() const;
	void prepareWarp(qsizetype desktop, bool fromActive = false);
	void warpToActive();
	void expectFocus(qsizetype desktop);

signals:
	void backAndForthChanged();
	void warpOnChangeChanged();
	void followMouseChanged();

private:
	explicit WorkspaceBehavior(QObject* parent);
	~WorkspaceBehavior() override = default;

	void onCurrentChanged();
	void tryWarp();
	void cancelWarp();
	void warpToTopmost();
	void watchTracker();
	void repairFocus();
	[[nodiscard]] static bool cursorOverShell();
	static void warpCursor(HWND hwnd);

	VirtualDesktops* desktops = nullptr;
	WindowTracker* tracker = nullptr;

	bool mBackAndForth = false;
	qint32 mWarpOnChange = 0;

	GUID currentId {};
	qsizetype currentIndex = -1;
	GUID previousId {};
	qsizetype previousIndex = -1;

	qsizetype warpDesktop = -1;
	qint64 warpDeadline = 0;
	QPointer<TrackedWindow> warpFrom;
	QTimer fallbackTimer;

	qsizetype focusDesktop = -1;
	QTimer focusTimer;

	void followTick();
	qint32 mFollowMouse = 0;
	QTimer followTimer;
	POINT followLast {};
	HWND followOver = nullptr;
};

class WindowManager: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(bool decorationsSupported READ decorationsSupported CONSTANT);
	Q_PROPERTY(QString borders READ borders WRITE setBorders NOTIFY bordersChanged);
	Q_PROPERTY(QColor activeBorderColor READ activeBorderColor WRITE setActiveBorderColor NOTIFY activeBorderColorChanged);
	Q_PROPERTY(QColor inactiveBorderColor READ inactiveBorderColor WRITE setInactiveBorderColor NOTIFY inactiveBorderColorChanged);
	Q_PROPERTY(QString corners READ corners WRITE setCorners NOTIFY cornersChanged);
	// "system" | "strip": remove the title bar Windows draws on other apps windows (see DecorationManager)
	Q_PROPERTY(QString titleBars READ titleBars WRITE setTitleBars NOTIFY titleBarsChanged);
	Q_PROPERTY(QStringList titleBarKeep READ titleBarKeep WRITE setTitleBarKeep NOTIFY titleBarKeepChanged);
	/// titleBars "skin": app windows (handles as strings) the shell draws its own header over (CaptionSkin)
	Q_PROPERTY(QStringList captionTargets READ captionTargets NOTIFY captionTargetsChanged);
	/// titleBars "skin": the header's height in logical px (0 = as tall as Windows' title bar was)
	Q_PROPERTY(qint32 captionHeight READ captionHeight WRITE setCaptionHeight NOTIFY captionHeightChanged);
	// "themed" title bars: their colour and their title colour (the inactive title is dimmed)
	Q_PROPERTY(QColor titleBarColor READ titleBarColor WRITE setTitleBarColor NOTIFY titleBarColorChanged);
	Q_PROPERTY(QColor titleBarTextColor READ titleBarTextColor WRITE setTitleBarTextColor NOTIFY titleBarTextColorChanged);
	Q_PROPERTY(bool workspaceBackAndForth READ workspaceBackAndForth WRITE setWorkspaceBackAndForth NOTIFY workspaceBackAndForthChanged);
	Q_PROPERTY(qint32 warpOnChangeWorkspace READ warpOnChangeWorkspace WRITE setWarpOnChangeWorkspace NOTIFY warpOnChangeWorkspaceChanged);
	Q_PROPERTY(qint32 followMouse READ followMouse WRITE setFollowMouse NOTIFY followMouseChanged);
	// Aether tiling: folder of komorebic.exe (empty = off) and whkd.exe; see VirtualDesktops::setKomorebi
	Q_PROPERTY(QString komorebi READ komorebi WRITE setKomorebi NOTIFY komorebiChanged);
	Q_PROPERTY(QString whkd READ whkd WRITE setWhkd NOTIFY komorebiChanged);
	// komorebi --hyprland: the Rust WM reads hyprland.conf and handles its binds (whkd is not started)
	Q_PROPERTY(bool komorebiHyprland READ komorebiHyprland WRITE setKomorebiHyprland NOTIFY komorebiChanged);
	// with komorebiHyprland: every SUPER bind also works with ALT (komorebi --hyprland-alt)
	Q_PROPERTY(bool komorebiHyprlandAlt READ komorebiHyprlandAlt WRITE setKomorebiHyprlandAlt NOTIFY komorebiChanged);
	// another app (a game, a fullscreen video) covers its whole monitor and has focus (checked every 500 ms)
	Q_PROPERTY(bool fullscreenApp READ fullscreenApp NOTIFY fullscreenAppChanged);
	// clang-format on

public:
	explicit WindowManager(QObject* parent = nullptr);

	[[nodiscard]] static bool decorationsSupported();
	[[nodiscard]] QString borders() const;
	void setBorders(const QString& borders);
	[[nodiscard]] QColor activeBorderColor() const;
	void setActiveBorderColor(const QColor& color);
	[[nodiscard]] QColor inactiveBorderColor() const;
	void setInactiveBorderColor(const QColor& color);
	[[nodiscard]] QString corners() const;
	void setCorners(const QString& corners);
	[[nodiscard]] QString titleBars() const;
	void setTitleBars(const QString& titleBars);
	[[nodiscard]] QStringList titleBarKeep() const;
	void setTitleBarKeep(const QStringList& keep);
	[[nodiscard]] QStringList captionTargets() const;
	[[nodiscard]] qint32 captionHeight() const;
	void setCaptionHeight(qint32 height);
	[[nodiscard]] QColor titleBarColor() const;
	void setTitleBarColor(const QColor& color);
	[[nodiscard]] QColor titleBarTextColor() const;
	void setTitleBarTextColor(const QColor& color);
	[[nodiscard]] bool workspaceBackAndForth() const;
	void setWorkspaceBackAndForth(bool enabled);
	[[nodiscard]] qint32 warpOnChangeWorkspace() const;
	void setWarpOnChangeWorkspace(qint32 mode);
	[[nodiscard]] qint32 followMouse() const;
	void setFollowMouse(qint32 mode);
	[[nodiscard]] QString komorebi() const { return this->mKomorebi; }
	void setKomorebi(const QString& dir);
	[[nodiscard]] QString whkd() const { return this->mWhkd; }
	void setWhkd(const QString& path);
	[[nodiscard]] bool komorebiHyprland() const { return this->mKomorebiHyprland; }
	void setKomorebiHyprland(bool hyprland);
	[[nodiscard]] bool komorebiHyprlandAlt() const { return this->mKomorebiHyprlandAlt; }
	void setKomorebiHyprlandAlt(bool alt);
	[[nodiscard]] static bool fullscreenApp();
	// from the panel stack's fullscreen check
	static void setFullscreenApp(bool active);

signals:
	void fullscreenAppChanged();
	void bordersChanged();
	void activeBorderColorChanged();
	void inactiveBorderColorChanged();
	void cornersChanged();
	void titleBarsChanged();
	void titleBarKeepChanged();
	void captionTargetsChanged();
	void captionHeightChanged();
	void titleBarColorChanged();
	void titleBarTextColorChanged();
	void workspaceBackAndForthChanged();
	void warpOnChangeWorkspaceChanged();
	void followMouseChanged();
	void komorebiChanged();

private:
	QString mKomorebi;
	QString mWhkd;
	bool mKomorebiHyprland = false;
	bool mKomorebiHyprlandAlt = false;
};

} // namespace qs::windows
