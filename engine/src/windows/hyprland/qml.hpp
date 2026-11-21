#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"
#include "../../core/qmlscreen.hpp"
#include "connection.hpp"
#include "monitor.hpp"

namespace qs::hyprland::ipc {

class HyprlandIpcQml: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(QString requestSocketPath READ requestSocketPath CONSTANT);
	Q_PROPERTY(QString eventSocketPath READ eventSocketPath CONSTANT);
	Q_PROPERTY(bool usingLua READ usingLua CONSTANT);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandMonitor* focusedMonitor READ default NOTIFY focusedMonitorChanged BINDABLE bindableFocusedMonitor);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandWorkspace* focusedWorkspace READ default NOTIFY focusedWorkspaceChanged BINDABLE bindableFocusedWorkspace);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandToplevel* activeToplevel READ default NOTIFY activeToplevelChanged BINDABLE bindableActiveToplevel);
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::hyprland::ipc::HyprlandMonitor>*);
	Q_PROPERTY(UntypedObjectModel* monitors READ monitors CONSTANT);
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::hyprland::ipc::HyprlandWorkspace>*);
	Q_PROPERTY(UntypedObjectModel* workspaces READ workspaces CONSTANT);
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::hyprland::ipc::HyprlandToplevel>*);
	Q_PROPERTY(UntypedObjectModel* toplevels READ toplevels CONSTANT);
	// clang-format on
	QML_NAMED_ELEMENT(Hyprland);
	QML_SINGLETON;

public:
	explicit HyprlandIpcQml();

	Q_INVOKABLE static void dispatch(const QString& request);

	Q_INVOKABLE static HyprlandMonitor* monitorFor(QuickshellScreenInfo* screen);

	Q_INVOKABLE static void refreshMonitors();

	Q_INVOKABLE static void refreshWorkspaces();

	Q_INVOKABLE static void refreshToplevels();

	[[nodiscard]] static QString requestSocketPath();
	[[nodiscard]] static QString eventSocketPath();
	[[nodiscard]] static bool usingLua() { return false; }
	[[nodiscard]] static QBindable<HyprlandMonitor*> bindableFocusedMonitor();
	[[nodiscard]] static QBindable<HyprlandWorkspace*> bindableFocusedWorkspace();
	[[nodiscard]] static QBindable<HyprlandToplevel*> bindableActiveToplevel();
	[[nodiscard]] static ObjectModel<HyprlandMonitor>* monitors();
	[[nodiscard]] static ObjectModel<HyprlandWorkspace>* workspaces();
	[[nodiscard]] static ObjectModel<HyprlandToplevel>* toplevels();

signals:
	void rawEvent(qs::hyprland::ipc::HyprlandIpcEvent* event);

	void focusedMonitorChanged();
	void focusedWorkspaceChanged();
	void activeToplevelChanged();
};

} // namespace qs::hyprland::ipc
