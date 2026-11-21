#pragma once

#include <string>

#include <qbytearray.h>
#include <qbytearrayview.h>
#include <qlocalsocket.h>
#include <qpair.h>
#include <qcontainerfwd.h>
#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qpointer.h>
#include <qpoint.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qscreen.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/model.hpp"
#include "../../core/qmlscreen.hpp"
#include "../virtual_desktops.hpp"
#include "../window_tracker.hpp"

namespace qs::hyprland::ipc {

class HyprlandMonitor;
class HyprlandWorkspace;
class HyprlandToplevel;
class Dispatcher;

} // namespace qs::hyprland::ipc

Q_DECLARE_OPAQUE_POINTER(qs::hyprland::ipc::HyprlandWorkspace*);
Q_DECLARE_OPAQUE_POINTER(qs::hyprland::ipc::HyprlandMonitor*);
Q_DECLARE_OPAQUE_POINTER(qs::hyprland::ipc::HyprlandToplevel*);

namespace qs::hyprland::ipc {

class HyprlandIpcEvent: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QString name READ nameStr CONSTANT);
	Q_PROPERTY(QString data READ dataStr CONSTANT);
	QML_NAMED_ELEMENT(HyprlandEvent);
	QML_UNCREATABLE("HyprlandIpcEvents cannot be created.");

public:
	HyprlandIpcEvent(QObject* parent): QObject(parent) {}

	Q_INVOKABLE [[nodiscard]] QVector<QString> parse(qint32 argumentCount) const;
	[[nodiscard]] QVector<QByteArrayView> parseView(qint32 argumentCount) const;

	[[nodiscard]] QString nameStr() const;
	[[nodiscard]] QString dataStr() const;

	QByteArray name;
	QByteArray data;
};

class HyprlandIpc: public QObject {
	Q_OBJECT;

public:
	static HyprlandIpc* instance();

	[[nodiscard]] QString requestSocketPath() const { return {}; }
	[[nodiscard]] QString eventSocketPath() const { return {}; }

	void dispatch(const QString& request);

	[[nodiscard]] HyprlandMonitor* monitorFor(QuickshellScreenInfo* screen);
	[[nodiscard]] HyprlandMonitor* monitorForScreen(QScreen* screen) const;
	[[nodiscard]] HyprlandMonitor* cursorMonitor() const;

	[[nodiscard]] QBindable<HyprlandMonitor*> bindableFocusedMonitor() const {
		return &this->bFocusedMonitor;
	}

	[[nodiscard]] QBindable<HyprlandWorkspace*> bindableFocusedWorkspace() const {
		return &this->bFocusedWorkspace;
	}

	[[nodiscard]] QBindable<HyprlandToplevel*> bindableActiveToplevel() const {
		return &this->bActiveToplevel;
	}

	[[nodiscard]] QBindable<quint32> bindableMonitorsVersion() const { return &this->bMonitorsVersion; }
	[[nodiscard]] QBindable<quint32> bindableWorkspacesVersion() const { return &this->bWorkspacesVersion; }

	// Hyprland window manager mode: while komorebi --hyprland serves Hyprland's sockets, workspaces, monitors'
	// active workspaces and the workspace of each window come from it (j/monitors, j/workspaces, j/clients,
	// refreshed on its events) instead of this emulation - so every shell sees the WM's real state
	[[nodiscard]] bool wmMode() const { return this->mWmMode; }
	[[nodiscard]] QBindable<quint32> bindableWmVersion() const { return &this->bWmVersion; }
	[[nodiscard]] HyprlandWorkspace* wmWorkspaceForAddress(quint64 address) const;

	[[nodiscard]] ObjectModel<HyprlandMonitor>* monitors();
	[[nodiscard]] ObjectModel<HyprlandWorkspace>* workspaces();
	[[nodiscard]] ObjectModel<HyprlandToplevel>* toplevels();

	[[nodiscard]] HyprlandWorkspace* workspaceById(qint32 id) const;
	[[nodiscard]] HyprlandWorkspace* workspaceByName(const QString& name) const;
	[[nodiscard]] HyprlandToplevel* findToplevelByAddress(quint64 address) const;
	[[nodiscard]] HyprlandToplevel* toplevelForWindow(qs::windows::TrackedWindow* window) const;

	[[nodiscard]] qs::windows::WindowTracker* tracker() const { return this->mTracker; }
	[[nodiscard]] qs::windows::VirtualDesktops* desktops() const { return this->mDesktops; }

	void refreshWorkspaces();
	void refreshMonitors();
	void refreshToplevels();

	void emitEvent(const QByteArray& name, const QByteArray& data);

	void scheduleIpcObjectRefresh(HyprlandToplevel* toplevel);
	void flushIpcObjects();

	void workspaceOccupancyChanged() { this->updateVisibleWorkspaces(); }

	[[nodiscard]] static QVector<QByteArrayView> parseEventArgs(QByteArrayView event, quint16 count);

signals:
	void connected();
	void rawEvent(HyprlandIpcEvent* event);

	void focusedMonitorChanged();
	void focusedWorkspaceChanged();
	void activeToplevelChanged();

	void dispatchGlobal(const QString& name);

	void toplevelAdded(HyprlandToplevel* toplevel);

private slots:
	void onWindowAdded(qs::windows::TrackedWindow* window);
	void onWindowRemoved(qs::windows::TrackedWindow* window);
	void onActiveWindowChanged();
	void onTrackerFlushed();
	void onDesktopsChanged();
	void onCurrentDesktopChanged();
	void onScreensChanged();
	void onButtonPressed(QPoint position, quint32 time);
	void onCursorMonitorChanged(quintptr monitor);
	void emitGeometryEvent();
	void emitTitleEvents();

private:
	explicit HyprlandIpc();
	Q_DISABLE_COPY_MOVE(HyprlandIpc);

	void syncMonitors(bool initial);
	void syncWorkspaces(bool initial);
	void updateVisibleWorkspaces(bool initial = false);
	void updateFocusedMonitor(HyprlandMonitor* monitor);
	void updateFocusedWorkspace();
	void scheduleGeometryEvent(bool prompt);

	qs::windows::WindowTracker* mTracker = nullptr;
	qs::windows::VirtualDesktops* mDesktops = nullptr;
	Dispatcher* dispatcher = nullptr;

	ObjectModel<HyprlandMonitor> mMonitors {this};
	QList<HyprlandWorkspace*> mAllWorkspaces;
	ObjectModel<HyprlandWorkspace> mWorkspaces {this};
	ObjectModel<HyprlandToplevel> mToplevels {this};
	QHash<qs::windows::TrackedWindow*, HyprlandToplevel*> byWindow;

	HyprlandIpcEvent event {this};
	QTimer geometryTimer;
	bool geometryAfterFlush = false;
	QTimer titleTimer;
	QList<QPointer<HyprlandToplevel>> pendingTitles;
	QTimer ipcObjectTimer;
	QList<QPointer<HyprlandToplevel>> dirtyIpcObjects;

	// Hyprland window manager link (see wmMode)
	void wmConnect();
	void wmReadEvents();
	void wmRefresh();
	void wmApply(const QByteArray& monitors, const QByteArray& workspaces, const QByteArray& clients);
	void wmLeave();
	QLocalSocket* wmSocket = nullptr;
	QString wmEventPath;
	std::wstring wmRequestPipe;
	QTimer wmReconnectTimer;
	QTimer wmRefreshTimer;
	bool mWmMode = false;
	bool wmRefreshing = false;
	bool wmRefreshAgain = false;
	QByteArray wmBuffer;
	QList<QPair<QByteArray, QByteArray>> wmPendingEvents;
	QHash<quint64, qint32> wmClientWorkspace;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandIpc, HyprlandMonitor*, bFocusedMonitor, &HyprlandIpc::focusedMonitorChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandIpc, HyprlandWorkspace*, bFocusedWorkspace, &HyprlandIpc::focusedWorkspaceChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandIpc, HyprlandToplevel*, bActiveToplevel, &HyprlandIpc::activeToplevelChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandIpc, quint32, bMonitorsVersion);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandIpc, quint32, bWorkspacesVersion);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandIpc, quint32, bWmVersion);
	// clang-format on
};

} // namespace qs::hyprland::ipc
