#pragma once

#include <qt_windows.h>

#include <qcolor.h>
#include <qdatetime.h>
#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::windows {

class TrackedWindow;
class WindowTracker;

class DecorationManager: public QObject {
	Q_OBJECT;

public:
	static DecorationManager* instance();
	~DecorationManager() override;
	Q_DISABLE_COPY_MOVE(DecorationManager);

	[[nodiscard]] static bool supported();
	static void restoreForCrash();

	[[nodiscard]] QString borders() const { return this->mBorders; }
	void setBorders(const QString& borders);
	[[nodiscard]] QColor activeColor() const { return this->mActiveColor; }
	void setActiveColor(const QColor& color);
	[[nodiscard]] QColor inactiveColor() const { return this->mInactiveColor; }
	void setInactiveColor(const QColor& color);
	[[nodiscard]] QString corners() const { return this->mCorners; }
	void setCorners(const QString& corners);
	// "system" (leave them) | "themed" (recolour them: titleBarColor / titleBarTextColor, a flat Linux-style
	// header) | "strip" (remove the title bar Windows draws on other apps' windows)
	[[nodiscard]] QString titleBars() const { return this->mTitleBars; }
	void setTitleBars(const QString& titleBars);
	[[nodiscard]] QColor titleBarColor() const { return this->mTitleBarColor; }
	void setTitleBarColor(const QColor& color);
	[[nodiscard]] QColor titleBarTextColor() const { return this->mTitleBarTextColor; }
	void setTitleBarTextColor(const QColor& color);
	// exe names ("notepad.exe") whose title bars are never stripped
	[[nodiscard]] QStringList titleBarKeep() const { return this->mTitleBarKeep; }
	void setTitleBarKeep(const QStringList& keep);
	// "skin" mode: the app windows (handles as decimal strings) whose title bar the shell draws over
	[[nodiscard]] QStringList captionTargets() const { return this->mCaptionTargets; }
	// skin: the header's height in logical px (0 = as tall as Windows' title bar was)
	[[nodiscard]] int captionHeight() const { return this->mCaptionHeight; }
	void setCaptionHeight(int height);
	// the header's height in physical px for this window (what its top edge was lowered by)
	[[nodiscard]] static int headerPx(HWND hwnd);

signals:
	void bordersChanged();
	void activeColorChanged();
	void inactiveColorChanged();
	void cornersChanged();
	void titleBarsChanged();
	void titleBarColorChanged();
	void titleBarTextColorChanged();
	void titleBarKeepChanged();
	void captionTargetsChanged();
	void captionHeightChanged();

private:
	explicit DecorationManager(QObject* parent);

	enum class BorderMode : quint8 { System, Colors, None };

	struct Applied {
		bool border = false;
		bool corner = false;
		COLORREF color = 0;
		DWORD preference = 0;
		bool stripped = false;
		bool darkened = false; // dark frame turned on by the strip (the resize edge left on top reads as a line)
		bool themed = false;   // caption / caption text recoloured
		COLORREF caption = 0;
		COLORREF text = 0;
		bool skinned = false; // Windows' title bar removed, the shell's header (CaptionSkin) is the window's top bar
		int shift = 0;        // px the window was lowered by so its content stayed put (the header sits there)
	};

	[[nodiscard]] BorderMode borderMode() const;
	[[nodiscard]] DWORD cornerPreference() const;
	[[nodiscard]] bool overriding() const;
	[[nodiscard]] bool stripping() const;
	[[nodiscard]] bool theming() const;
	[[nodiscard]] bool skinning() const;
	[[nodiscard]] bool settled(HWND hwnd);
	void publishTargets();
	[[nodiscard]] bool shouldStrip(TrackedWindow* window) const;
	static void restoreState(HWND hwnd, const Applied& state);

	void update();
	void start();
	void stop();
	void applyAll();
	void apply(TrackedWindow* window);
	void forget(TrackedWindow* window);
	void restoreAll();
	void onActiveWindowChanged();

	WindowTracker* tracker = nullptr;
	QList<QMetaObject::Connection> connections;
	QHash<HWND, Applied> applied;
	HWND lastActive = nullptr;
	bool running = false;

	QString mBorders = QStringLiteral("system");
	QColor mActiveColor;
	QColor mInactiveColor;
	QString mCorners = QStringLiteral("default");
	QString mTitleBars = QStringLiteral("system");
	QColor mTitleBarColor;
	QColor mTitleBarTextColor;
	QStringList mTitleBarKeep;
	QStringList mCaptionTargets;
	int mCaptionHeight = 0;
	QHash<HWND, qint64> firstSeen;
};

} // namespace qs::windows
