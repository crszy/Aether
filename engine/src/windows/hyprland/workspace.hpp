#pragma once

#include <qcontainerfwd.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "connection.hpp"
#include "hyprland_toplevel.hpp"

namespace qs::hyprland::ipc {

class HyprlandMonitor;

class HyprlandWorkspace: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(QString address READ default NOTIFY addressChanged BINDABLE bindableAddress);
	Q_PROPERTY(qint32 id READ default NOTIFY idChanged BINDABLE bindableId);
	Q_PROPERTY(QString name READ default NOTIFY nameChanged BINDABLE bindableName);
	Q_PROPERTY(bool active READ default NOTIFY activeChanged BINDABLE bindableActive);
	Q_PROPERTY(bool focused READ default NOTIFY focusedChanged BINDABLE bindableFocused);
	Q_PROPERTY(bool urgent READ default NOTIFY urgentChanged BINDABLE bindableUrgent);
	Q_PROPERTY(bool hasFullscreen READ default NOTIFY hasFullscreenChanged BINDABLE bindableHasFullscreen);
	Q_PROPERTY(QVariantMap lastIpcObject READ lastIpcObject NOTIFY lastIpcObjectChanged);
	Q_PROPERTY(qs::hyprland::ipc::HyprlandMonitor* monitor READ default NOTIFY monitorChanged BINDABLE bindableMonitor);
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::hyprland::ipc::HyprlandToplevel*);
	Q_PROPERTY(UntypedObjectModel* toplevels READ toplevels CONSTANT);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("HyprlandWorkspaces must be retrieved from the HyprlandIpc object.");

public:
	explicit HyprlandWorkspace(HyprlandIpc* ipc);

	void updateInitial(qint32 id, const QString& name);
	void refreshIpcObject();
	// Hyprland window manager mode: monitor / active / focused come from the WM
	void setWm(HyprlandMonitor* monitor, bool active, bool focused);

	Q_INVOKABLE void activate();

	[[nodiscard]] QBindable<QString> bindableAddress() { return &this->bAddress; }
	[[nodiscard]] QBindable<qint32> bindableId() { return &this->bId; }
	[[nodiscard]] QBindable<QString> bindableName() { return &this->bName; }
	[[nodiscard]] QBindable<bool> bindableActive() { return &this->bActive; }
	[[nodiscard]] QBindable<bool> bindableFocused() { return &this->bFocused; }
	[[nodiscard]] QBindable<bool> bindableUrgent() { return &this->bUrgent; }
	[[nodiscard]] QBindable<bool> bindableHasFullscreen() { return &this->bHasFullscreen; }
	[[nodiscard]] QBindable<HyprlandMonitor*> bindableMonitor() { return &this->bMonitor; }
	[[nodiscard]] ObjectModel<HyprlandToplevel>* toplevels() { return &this->mToplevels; }

	[[nodiscard]] QVariantMap lastIpcObject() const { return this->mLastIpcObject; }

	void insertToplevel(HyprlandToplevel* toplevel);
	void removeToplevel(HyprlandToplevel* toplevel);

signals:
	void addressChanged();
	void idChanged();
	void nameChanged();
	void activeChanged();
	void focusedChanged();
	void urgentChanged();
	void hasFullscreenChanged();
	void lastIpcObjectChanged();
	void monitorChanged();

private slots:
	void updateFullscreen();

private:
	HyprlandIpc* ipc;
	QVariantMap mLastIpcObject;

	ObjectModel<HyprlandToplevel> mToplevels {this};

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, QString, bAddress, &HyprlandWorkspace::addressChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(HyprlandWorkspace, qint32, bId, -1, &HyprlandWorkspace::idChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, QString, bName, &HyprlandWorkspace::nameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, bool, bActive, &HyprlandWorkspace::activeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, bool, bFocused, &HyprlandWorkspace::focusedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, bool, bUrgent, &HyprlandWorkspace::urgentChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, bool, bHasFullscreen, &HyprlandWorkspace::hasFullscreenChanged);
	Q_OBJECT_BINDABLE_PROPERTY(HyprlandWorkspace, HyprlandMonitor*, bMonitor, &HyprlandWorkspace::monitorChanged);
	// clang-format on
};

} // namespace qs::hyprland::ipc
