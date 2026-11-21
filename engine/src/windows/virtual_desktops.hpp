#pragma once

#include <qt_windows.h>
#include <functional>

#include <qhash.h>
#include <qlocalserver.h>
#include <qlist.h>
#include <qobject.h>
#include <qset.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

struct IVirtualDesktopManager;

namespace qs::windows {

class RegistryWatcher: public QThread {
	Q_OBJECT;

public:
	explicit RegistryWatcher(QList<QString> subkeys, QObject* parent = nullptr);
	~RegistryWatcher() override;
	Q_DISABLE_COPY_MOVE(RegistryWatcher);

	void stop();

signals:
	void changed();

protected:
	void run() override;

private:
	QList<QString> subkeys;
	HANDLE stopEvent = nullptr;
};

class VirtualDesktops: public QObject {
	Q_OBJECT;

public:
	// the mode komorebi runs in now (a reloaded shell starts from it instead of "off", which restarted komorebi)
	[[nodiscard]] bool hyprlandMode() const { return this->komorebiHyprland; }
	[[nodiscard]] bool hyprlandAltMode() const { return this->komorebiHyprlandAlt; }
	struct Desktop {
		GUID id {};
		QString name;
	};

	static VirtualDesktops* instance();

	[[nodiscard]] const QList<Desktop>& desktops() const { return this->mDesktops; }
	[[nodiscard]] qsizetype count() const { return this->mDesktops.length(); }
	[[nodiscard]] qsizetype currentIndex() const { return this->mCurrent; }
	[[nodiscard]] qsizetype indexOf(const GUID& id) const;
	[[nodiscard]] bool accessorLoaded() const { return this->accessor.loaded; }

	[[nodiscard]] GUID windowDesktopId(HWND hwnd) const;
	[[nodiscard]] qsizetype windowDesktopIndex(HWND hwnd) const;
	[[nodiscard]] bool isWindowOnCurrent(HWND hwnd) const;

	bool switchTo(qsizetype index);
	void ensureCount(qsizetype count, std::function<void(bool)> done);
	bool removeDesktop(qsizetype index, qsizetype fallback);
	bool moveWindow(HWND hwnd, qsizetype index);
	bool pinWindow(HWND hwnd, bool pinned);
	[[nodiscard]] bool isWindowPinned(HWND hwnd) const;

	void refresh(bool rereadNames = false);
	void noteCloakChange();

	// Caelestia's own workspaces: used when Windows' desktop service is not there (explorer.exe is not running,
	// because Caelestia is the shell) - windows on other workspaces are hidden, like Hyprland's workspaces
	[[nodiscard]] bool ownWorkspaces() const { return this->own; }
	[[nodiscard]] bool hidesWindow(HWND hwnd) const;

	// Hyprland's special workspaces (scratchpads): windows sent there are hidden until the workspace is toggled
	// open over the current one. A window there reports desktop index -2 - id.
	bool moveToSpecial(HWND hwnd, const QString& name);
	bool toggleSpecial(const QString& name);
	[[nodiscard]] QString shownSpecial() const { return this->mShownSpecial; }
	[[nodiscard]] QString specialName(qsizetype index) const;

	// Aether tiling: komorebi (the fork) owns the workspaces. Caelestia starts it (and whkd) when they don't run,
	// subscribes to its state over a named pipe, shows its workspaces (focused monitor) and asks it to switch.
	void setKomorebi(const QString& dir, const QString& whkd, bool hyprland = false, bool hyprlandAlt = false);
	// Hyprland mode of the Rust WM (komorebi --hyprland): it handles the keybinds itself, so no whkd. Restarts komorebi
	// (on a worker thread) when it changes while Aether tiling is on.
	void setKomorebiHyprland(bool hyprland, bool hyprlandAlt, const QString& whkd);
	[[nodiscard]] bool komorebiMode() const { return this->ext; }

	static QString guidToString(const GUID& guid);

signals:
	void desktopsChanged();
	void currentChanged();
	void accessorReady();

private:
	explicit VirtualDesktops();
	~VirtualDesktops() override;
	Q_DISABLE_COPY_MOVE(VirtualDesktops);

	[[nodiscard]] IVirtualDesktopManager* manager() const;
	void loadAccessor();
	void installListener();
	bool readRegistry(QList<GUID>& ids, GUID& current) const;
	[[nodiscard]] QString readDesktopName(const GUID& id) const;
	void createNext();
	void pollCreation();
	void finishCreation();
	void sendShortcut(WORD key, int times) const;

	template <typename R, typename... P, typename... A>
	R call(R fallback, R (*fn)(P...), A... args) const;
	void accessorFailed() const;

	static LRESULT CALLBACK listenerProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	QList<Desktop> mDesktops;
	qsizetype mCurrent = -1;
	GUID mCurrentId {};
	DWORD sessionId = 0;

	mutable IVirtualDesktopManager* mManager = nullptr;
	mutable bool managerTried = false;
	RegistryWatcher* watcher = nullptr;
	HWND listener = nullptr;
	QTimer cloakTimer;
	QTimer creationTimer;
	qsizetype creationTarget = 0;
	qsizetype creationBefore = 0;
	int creationPolls = 0;
	QList<QPair<qsizetype, std::function<void(bool)>>> creationWaiters;

	mutable ULONGLONG accessorRestartedAt = 0;

	void enableOwn();
	void showOwn(HWND hwnd, bool show);
	void showAllOwn();
	[[nodiscard]] static GUID ownGuid(qsizetype index);
	[[nodiscard]] QList<Desktop> ownList(qsizetype count) const;
	bool own = false;
	mutable QHash<HWND, qsizetype> ownOf;          // window -> workspace index
	QSet<HWND> ownPinned;                          // on every workspace
	QStringList specials;                          // special workspace names; index -2 - i
	QString mShownSpecial;
	QHash<HWND, QList<HWND>> ownHidden;            // hidden by us -> its dialogs hidden with it
	QHash<qsizetype, HWND> ownLastFocus;
	QList<HWND> leftHidden;                        // hidden by an earlier run (crash or restart)

	void komorebiSubscribe();
	void komorebiState(const QByteArray& json);
	void runKomorebic(const QStringList& args) const;
	bool ext = false;
	bool komorebiHyprland = false;
	bool komorebiHyprlandAlt = false;
	QString komorebiDir;
	QLocalServer* komorebiServer = nullptr;
	QTimer komorebiRetry;
	QByteArray komorebiBuffer;
	QHash<HWND, qsizetype> extOf;                  // window -> its workspace (on its monitor)

	mutable struct Accessor {
		HMODULE module = nullptr;
		bool loaded = false;
		int (*getCurrentDesktopNumber)() = nullptr;
		int (*getDesktopCount)() = nullptr;
		GUID (*getDesktopIdByNumber)(int) = nullptr;
		int (*getWindowDesktopNumber)(HWND) = nullptr;
		int (*goToDesktopNumber)(int) = nullptr;
		int (*moveWindowToDesktopNumber)(HWND, int) = nullptr;
		int (*isWindowOnCurrentVirtualDesktop)(HWND) = nullptr;
		int (*createDesktop)() = nullptr;
		int (*removeDesktop)(int, int) = nullptr;
		int (*registerPostMessageHook)(HWND, UINT) = nullptr;
		int (*unregisterPostMessageHook)(HWND) = nullptr;
		int (*isPinnedWindow)(HWND) = nullptr;
		int (*pinWindow)(HWND) = nullptr;
		int (*unPinWindow)(HWND) = nullptr;
		void (*restart)() = nullptr;
	} accessor;
};

} // namespace qs::windows
