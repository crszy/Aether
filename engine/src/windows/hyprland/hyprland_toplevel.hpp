#pragma once

#include <qcontainerfwd.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../wayland/toplevel.hpp"
#include "../window_tracker.hpp"
#include "connection.hpp"

namespace qs::hyprland::ipc {

class HyprlandToplevel: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("");
	QML_ATTACHED(HyprlandToplevel);
	// clang-format off
	Q_PROPERTY(QString address READ addressStr NOTIFY addressChanged);
	Q_PROPERTY(HyprlandToplevel* handle READ hyprlandHandle NOTIFY hyprlandHandleChanged);
	Q_PROPERTY(qs::wayland::toplevel::Toplevel* wayland READ waylandHandle NOTIFY waylandHandleChanged);
	Q_PROPERTY(QString title READ default NOTIFY titleChanged BINDABLE bindableTitle);
	Q_PROPERTY(bool activated READ default NOTIFY activatedChanged BINDABLE bindableActivated);
	Q_PROPERTY(bool urgent READ default NOTIFY urgentChanged BINDABLE bindableUrgent);
	Q_PROPERTY(QVariantMap lastIpcObject READ default BINDABLE bindableLastIpcObject NOTIFY lastIpcObjectChanged);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandWorkspace* workspace READ default NOTIFY workspaceChanged BINDABLE bindableWorkspace);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandMonitor* monitor READ default NOTIFY monitorChanged BINDABLE bindableMonitor);
	// clang-format on

public:
	explicit HyprlandToplevel(HyprlandIpc* ipc, qs::windows::TrackedWindow* window);
	explicit HyprlandToplevel(HyprlandIpc* ipc, qs::wayland::toplevel::Toplevel* toplevel);

	static HyprlandToplevel* qmlAttachedProperties(QObject* object);

	[[nodiscard]] QString addressStr() const { return QString::number(this->mAddress, 16); }
	[[nodiscard]] quint64 address() const { return this->mAddress; }
	[[nodiscard]] qs::windows::TrackedWindow* window() const { return this->mWindow; }

	[[nodiscard]] HyprlandToplevel* hyprlandHandle();
	[[nodiscard]] qs::wayland::toplevel::Toplevel* waylandHandle();

	[[nodiscard]] QBindable<QString> bindableTitle() { return &this->bTitle; }
	[[nodiscard]] QBindable<bool> bindableActivated() { return &this->bActivated; }
	[[nodiscard]] QBindable<bool> bindableUrgent() { return &this->bUrgent; }

	[[nodiscard]] QBindable<QVariantMap> bindableLastIpcObject() const {
		return &this->bLastIpcObject;
	};

	[[nodiscard]] QBindable<HyprlandWorkspace*> bindableWorkspace() { return &this->bWorkspace; }
	[[nodiscard]] QBindable<HyprlandMonitor*> bindableMonitor() { return &this->bMonitor; }

	void refreshIpcObject();
	void markIpcObjectDirty();
	void flushIpcObject();
	void leaveWorkspace();

signals:
	void addressChanged();
	QSDOC_HIDE void waylandHandleChanged();
	QSDOC_HIDE void hyprlandHandleChanged();

	void titleChanged();
	void activatedChanged();
	void urgentChanged();
	void workspaceChanged();
	void monitorChanged();
	void lastIpcObjectChanged();

private slots:
	void onWorkspaceChanged();
	void onToplevelAdded(HyprlandToplevel* toplevel);

private:
	void setHyprlandHandle(HyprlandToplevel* handle);
	void bindToHandle();

	quint64 mAddress = 0;
	HyprlandIpc* ipc;

	qs::windows::TrackedWindow* mWindow = nullptr;
	qs::wayland::toplevel::Toplevel* mWaylandHandle = nullptr;
	HyprlandToplevel* mHyprlandHandle = nullptr;
	HyprlandWorkspace* memberOf = nullptr;
	bool ipcObjectDirty = false;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandToplevel, QString, bTitle, &HyprlandToplevel::titleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandToplevel, bool, bActivated, &HyprlandToplevel::activatedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandToplevel, bool, bUrgent, &HyprlandToplevel::urgentChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandToplevel, HyprlandWorkspace*, bWorkspace, &HyprlandToplevel::workspaceChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandToplevel, HyprlandMonitor*, bMonitor, &HyprlandToplevel::monitorChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandToplevel, QVariantMap, bLastIpcObject, &HyprlandToplevel::lastIpcObjectChanged);
	// clang-format on
};

} // namespace qs::hyprland::ipc
