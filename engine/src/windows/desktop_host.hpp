#pragma once

#include <qt_windows.h>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstringlist.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qwineventnotifier.h>

namespace qs::windows {

class DesktopHost: public QObject {
	Q_OBJECT;

public:
	static DesktopHost* instance();
	~DesktopHost() override;
	Q_DISABLE_COPY_MOVE(DesktopHost);

	[[nodiscard]] bool enabled() const { return this->mEnabled; }
	void setEnabled(bool enabled);

	[[nodiscard]] HWND parentWindow();
	[[nodiscard]] HWND insertAfter() const { return this->mInsertAfter; }
	[[nodiscard]] HWND iconsHost();

	[[nodiscard]] QStringList aboveIcons() const { return this->mAboveIcons; }
	void setAboveIcons(const QStringList& namespaces);

	[[nodiscard]] bool active() const { return this->mEnabled && this->mParent != nullptr; }

signals:
	void enabledChanged();
	void activeChanged();
	void parentChanged();
	void parentMoved();
	void aboveIconsChanged();
	void iconsRestacked();
	void wallpaperChanged();

private:
	explicit DesktopHost(QObject* parent);

	void ensureListener();
	void watchWallpaperKey();
	void scheduleRefresh(int delayMs = 0);
	void refresh();
	void lookup();
	void installHook();
	void removeHook();

	static LRESULT CALLBACK listenerProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
	static void CALLBACK eventProc(
	    HWINEVENTHOOK hook,
	    DWORD event,
	    HWND hwnd,
	    LONG idObject,
	    LONG idChild,
	    DWORD thread,
	    DWORD time
	);

	bool mEnabled = false;
	bool lookedUp = false;
	HWND mParent = nullptr;
	HWND mInsertAfter = nullptr;
	HWND mIconsView = nullptr;
	HWND mIconsHost = nullptr;
	QStringList mAboveIcons;
	HWND listener = nullptr;
	HKEY desktopKey = nullptr;
	HANDLE desktopKeyEvent = nullptr;
	QWinEventNotifier* desktopKeyNotifier = nullptr;
	HWINEVENTHOOK hook = nullptr;
	HWINEVENTHOOK moveHook = nullptr;
	DWORD hookThread = 0;
	bool movePending = false;
	bool restackPending = false;
	int spawnRetriesLeft = 4;
	QTimer refreshTimer;
};

class DesktopLayer: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged);
	Q_PROPERTY(bool active READ active NOTIFY activeChanged);
	Q_PROPERTY(QStringList aboveIcons READ aboveIcons WRITE setAboveIcons NOTIFY aboveIconsChanged);
	// clang-format on

public:
	explicit DesktopLayer(QObject* parent = nullptr);

	[[nodiscard]] bool enabled() const;
	void setEnabled(bool enabled);

	[[nodiscard]] bool active() const;

	[[nodiscard]] QStringList aboveIcons() const;
	void setAboveIcons(const QStringList& namespaces);

signals:
	void enabledChanged();
	void activeChanged();
	void aboveIconsChanged();
	void wallpaperChanged();
};

} // namespace qs::windows
