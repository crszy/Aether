#include "workspace.hpp"
#include <algorithm>

#include <qcontainerfwd.h>
#include <qobject.h>
#include <qproperty.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../window_tracker.hpp"
#include "connection.hpp"
#include "hyprland_toplevel.hpp"
#include "monitor.hpp"

namespace qs::hyprland::ipc {

HyprlandWorkspace::HyprlandWorkspace(HyprlandIpc* ipc): QObject(ipc), ipc(ipc) {
	Qt::beginPropertyUpdateGroup();

	this->bActive.setBinding([this]() { return this->ipc->bindableFocusedWorkspace().value() == this; });
	this->bFocused.setBinding([this]() { return this->ipc->bindableFocusedWorkspace().value() == this; });
	this->bMonitor.setBinding([this]() { return this->ipc->bindableFocusedMonitor().value(); });
	this->bAddress.setBinding([this]() { return QString::number(this->bId.value()); });

	Qt::endPropertyUpdateGroup();

	// clang-format off
	QObject::connect(this, &HyprlandWorkspace::nameChanged, this, &HyprlandWorkspace::refreshIpcObject);
	QObject::connect(this, &HyprlandWorkspace::activeChanged, this, &HyprlandWorkspace::refreshIpcObject);
	QObject::connect(this, &HyprlandWorkspace::monitorChanged, this, &HyprlandWorkspace::refreshIpcObject);
	QObject::connect(this, &HyprlandWorkspace::hasFullscreenChanged, this, &HyprlandWorkspace::refreshIpcObject);
	QObject::connect(&this->mToplevels, &UntypedObjectModel::valuesChanged, this, &HyprlandWorkspace::updateFullscreen);
	// clang-format on
}

void HyprlandWorkspace::updateInitial(qint32 id, const QString& name) {
	Qt::beginPropertyUpdateGroup();
	this->bId = id;
	this->bName = name;
	Qt::endPropertyUpdateGroup();
	this->refreshIpcObject();
}

void HyprlandWorkspace::setWm(HyprlandMonitor* monitor, bool active, bool focused) {
	Qt::beginPropertyUpdateGroup();
	if (this->bMonitor.hasBinding() || this->bMonitor.value() != monitor) this->bMonitor = monitor;
	if (this->bActive.hasBinding() || this->bActive.value() != active) this->bActive = active;
	if (this->bFocused.hasBinding() || this->bFocused.value() != focused) this->bFocused = focused;
	Qt::endPropertyUpdateGroup();
	this->refreshIpcObject();
}

void HyprlandWorkspace::activate() {
	this->ipc->dispatch(QString("workspace %1").arg(this->bId.value()));
}

void HyprlandWorkspace::insertToplevel(HyprlandToplevel* toplevel) {
	if (toplevel == nullptr) return;

	const auto& list = this->mToplevels.valueList();
	if (std::ranges::find(list, toplevel) != list.end()) return;

	this->mToplevels.insertObject(toplevel);
	if (list.length() == 1) this->ipc->workspaceOccupancyChanged();

	if (auto* window = toplevel->window()) {
		QObject::connect(
		    window,
		    &qs::windows::TrackedWindow::fullscreenChanged,
		    this,
		    &HyprlandWorkspace::updateFullscreen
		);
	}

	this->updateFullscreen();
}

void HyprlandWorkspace::removeToplevel(HyprlandToplevel* toplevel) {
	if (toplevel == nullptr) return;

	if (auto* window = toplevel->window()) QObject::disconnect(window, nullptr, this, nullptr);
	if (!this->mToplevels.removeObject(toplevel)) return;
	this->updateFullscreen();
	if (this->mToplevels.valueList().isEmpty()) this->ipc->workspaceOccupancyChanged();
}

void HyprlandWorkspace::updateFullscreen() {
	const auto& list = this->mToplevels.valueList();

	auto hasFullscreen = std::ranges::any_of(list, [](HyprlandToplevel* toplevel) {
		auto* window = toplevel->window();
		return window != nullptr && window->fullscreen();
	});

	if (hasFullscreen != this->bHasFullscreen.value()) this->bHasFullscreen = hasFullscreen;
	this->refreshIpcObject();
}

void HyprlandWorkspace::refreshIpcObject() {
	auto* monitor = this->bMonitor.value();
	const auto& list = this->mToplevels.valueList();

	HyprlandToplevel* last = nullptr;
	for (auto* toplevel: list) {
		if (toplevel->bindableActivated().value()) last = toplevel;
	}

	QVariantMap object {
	    {"id", this->bId.value()},
	    {"name", this->bName.value()},
	    {"monitor", monitor == nullptr ? QString() : monitor->bindableName().value()},
	    {"monitorID", monitor == nullptr ? -1 : monitor->bindableId().value()},
	    {"windows", static_cast<int>(list.length())},
	    {"hasfullscreen", this->bHasFullscreen.value()},
	    {"lastwindow", last == nullptr ? QString("0x0") : "0x" + last->addressStr()},
	    {"lastwindowtitle", last == nullptr ? QString() : last->bindableTitle().value()},
	    {"ispersistent", false},
	};

	if (object == this->mLastIpcObject) return;
	this->mLastIpcObject = object;
	emit this->lastIpcObjectChanged();
}

} // namespace qs::hyprland::ipc
