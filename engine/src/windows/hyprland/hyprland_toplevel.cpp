#include "hyprland_toplevel.hpp"

#include <qcontainerfwd.h>
#include <qlist.h>
#include <qobject.h>
#include <qproperty.h>
#include <qrect.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../tiling.hpp"
#include "../virtual_desktops.hpp"
#include "../wayland/toplevel.hpp"
#include "../window_tracker.hpp"
#include "connection.hpp"
#include "monitor.hpp"
#include "workspace.hpp"

using namespace qs::wayland::toplevel;
using namespace qs::windows;

namespace qs::hyprland::ipc {

HyprlandToplevel::HyprlandToplevel(HyprlandIpc* ipc, TrackedWindow* window)
    : QObject(ipc)
    , mAddress(window->address())
    , ipc(ipc)
    , mWindow(window) {
	this->bTitle.setBinding([window]() { return window->bindableTitle().value(); });
	this->bActivated.setBinding([window]() { return window->bindableActivated().value(); });

	this->bWorkspace.setBinding([this, window]() -> HyprlandWorkspace* {
		this->ipc->bindableWorkspacesVersion().value();
		this->ipc->bindableWmVersion().value();
		// the Hyprland window manager knows where every window really is
		if (this->ipc->wmMode()) {
			if (auto* workspace = this->ipc->wmWorkspaceForAddress(this->mAddress)) return workspace;
			return this->ipc->bindableFocusedWorkspace().value();
		}
		auto desktop = window->bindableDesktop().value();
		if (desktop < 0) return this->ipc->bindableFocusedWorkspace().value();
		return this->ipc->workspaceById(desktop + 1);
	});

	this->bMonitor.setBinding([this, window]() -> HyprlandMonitor* {
		this->ipc->bindableMonitorsVersion().value();
		return this->ipc->monitorForScreen(window->bindableScreen().value());
	});

	QObject::connect(this, &HyprlandToplevel::workspaceChanged, this, &HyprlandToplevel::onWorkspaceChanged);

	// clang-format off
	QObject::connect(window, &TrackedWindow::titleChanged, this, &HyprlandToplevel::markIpcObjectDirty);
	QObject::connect(window, &TrackedWindow::appIdChanged, this, &HyprlandToplevel::markIpcObjectDirty);
	QObject::connect(window, &TrackedWindow::pidChanged, this, &HyprlandToplevel::markIpcObjectDirty);
	QObject::connect(window, &TrackedWindow::rectChanged, this, &HyprlandToplevel::markIpcObjectDirty);
	QObject::connect(window, &TrackedWindow::minimizedChanged, this, &HyprlandToplevel::markIpcObjectDirty);
	QObject::connect(window, &TrackedWindow::maximizedChanged, this, &HyprlandToplevel::markIpcObjectDirty);
	QObject::connect(window, &TrackedWindow::fullscreenChanged, this, &HyprlandToplevel::markIpcObjectDirty);
	QObject::connect(window, &TrackedWindow::activatedChanged, this, &HyprlandToplevel::markIpcObjectDirty);
	QObject::connect(this, &HyprlandToplevel::workspaceChanged, this, &HyprlandToplevel::markIpcObjectDirty);
	QObject::connect(this, &HyprlandToplevel::monitorChanged, this, &HyprlandToplevel::markIpcObjectDirty);
	// clang-format on

	QObject::connect(
	    TilingManager::instance(),
	    &TilingManager::windowTilingChanged,
	    this,
	    [this](TrackedWindow* changed) {
		    if (changed == this->mWindow) this->markIpcObjectDirty();
	    }
	);

	this->onWorkspaceChanged();
	this->refreshIpcObject();
}

HyprlandToplevel::HyprlandToplevel(HyprlandIpc* ipc, Toplevel* toplevel)
    : QObject(toplevel)
    , mAddress(toplevel->window()->address())
    , ipc(ipc)
    , mWaylandHandle(toplevel) {
	this->setHyprlandHandle(ipc->toplevelForWindow(toplevel->window()));

	if (this->mHyprlandHandle == nullptr) {
		QObject::connect(ipc, &HyprlandIpc::toplevelAdded, this, &HyprlandToplevel::onToplevelAdded);
	}
}

HyprlandToplevel* HyprlandToplevel::qmlAttachedProperties(QObject* object) {
	if (auto* toplevel = qobject_cast<Toplevel*>(object)) {
		return new HyprlandToplevel(HyprlandIpc::instance(), toplevel);
	}

	return nullptr;
}

HyprlandToplevel* HyprlandToplevel::hyprlandHandle() {
	return this->mWindow != nullptr ? this : this->mHyprlandHandle;
}

Toplevel* HyprlandToplevel::waylandHandle() {
	if (this->mWaylandHandle != nullptr) return this->mWaylandHandle;
	return ToplevelManager::instance()->forWindow(this->mWindow);
}

void HyprlandToplevel::onToplevelAdded(HyprlandToplevel* toplevel) {
	if (toplevel->address() != this->mAddress) return;
	this->setHyprlandHandle(toplevel);
	QObject::disconnect(this->ipc, &HyprlandIpc::toplevelAdded, this, nullptr);
}

void HyprlandToplevel::setHyprlandHandle(HyprlandToplevel* handle) {
	if (this->mHyprlandHandle == handle) return;

	if (this->mHyprlandHandle != nullptr) {
		QObject::disconnect(this->mHyprlandHandle, nullptr, this, nullptr);
	}

	this->mHyprlandHandle = handle;

	if (handle != nullptr) {
		QObject::connect(handle, &QObject::destroyed, this, [this]() {
			this->mHyprlandHandle = nullptr;
			this->bindToHandle();
			emit this->hyprlandHandleChanged();
		});
	}

	this->bindToHandle();
	emit this->hyprlandHandleChanged();
}

void HyprlandToplevel::bindToHandle() {
	auto* handle = this->mHyprlandHandle;

	auto sync = [this, handle]() {
		Qt::beginPropertyUpdateGroup();
		this->bTitle = handle->bindableTitle().value();
		this->bActivated = handle->bindableActivated().value();
		this->bUrgent = handle->bindableUrgent().value();
		this->bWorkspace = handle->bindableWorkspace().value();
		this->bMonitor = handle->bindableMonitor().value();
		this->bLastIpcObject = handle->bindableLastIpcObject().value();
		Qt::endPropertyUpdateGroup();
	};

	if (handle == nullptr) {
		Qt::beginPropertyUpdateGroup();
		this->bTitle = QString();
		this->bActivated = false;
		this->bUrgent = false;
		this->bWorkspace = nullptr;
		this->bMonitor = nullptr;
		this->bLastIpcObject = QVariantMap();
		Qt::endPropertyUpdateGroup();
		return;
	}

	// clang-format off
	QObject::connect(handle, &HyprlandToplevel::titleChanged, this, sync);
	QObject::connect(handle, &HyprlandToplevel::activatedChanged, this, sync);
	QObject::connect(handle, &HyprlandToplevel::urgentChanged, this, sync);
	QObject::connect(handle, &HyprlandToplevel::workspaceChanged, this, sync);
	QObject::connect(handle, &HyprlandToplevel::monitorChanged, this, sync);
	QObject::connect(handle, &HyprlandToplevel::lastIpcObjectChanged, this, sync);
	// clang-format on

	sync();
}

void HyprlandToplevel::onWorkspaceChanged() {
	auto* workspace = this->bWorkspace.value();
	if (workspace == this->memberOf) return;

	if (this->memberOf != nullptr) {
		QObject::disconnect(this->memberOf, &QObject::destroyed, this, nullptr);
		this->memberOf->removeToplevel(this);
	}

	this->memberOf = workspace;

	if (workspace != nullptr) {
		QObject::connect(workspace, &QObject::destroyed, this, [this, workspace]() {
			if (this->memberOf == workspace) this->memberOf = nullptr;
		});

		workspace->insertToplevel(this);
	}
}

void HyprlandToplevel::leaveWorkspace() {
	if (this->memberOf == nullptr) return;
	QObject::disconnect(this->memberOf, &QObject::destroyed, this, nullptr);
	this->memberOf->removeToplevel(this);
	this->memberOf = nullptr;
}

void HyprlandToplevel::markIpcObjectDirty() {
	if (this->ipcObjectDirty || this->mWindow == nullptr) return;
	this->ipcObjectDirty = true;
	this->ipc->scheduleIpcObjectRefresh(this);
}

void HyprlandToplevel::flushIpcObject() {
	if (this->ipcObjectDirty) this->refreshIpcObject();
}

void HyprlandToplevel::refreshIpcObject() {
	this->ipcObjectDirty = false;

	auto* window = this->mWindow;
	if (window == nullptr) return;

	auto rect = window->rect();
	auto* workspace = this->bWorkspace.value();
	auto* monitor = this->bMonitor.value();

	auto fullscreen = window->fullscreen() ? 2 : (window->maximized() ? 1 : 0);

	auto* tiling = TilingManager::active();
	auto floating = tiling != nullptr ? !tiling->isTiled(window) : fullscreen == 0;

	QVariantMap object {
	    {"address", "0x" + this->addressStr()},
	    {"mapped", true},
	    {"hidden", VirtualDesktops::instance()->hidesWindow(window->hwnd())},
	    {"at", QVariantList {rect.x(), rect.y()}},
	    {"size", QVariantList {rect.width(), rect.height()}},
	    {"workspace",
	     QVariantMap {
	         {"id", workspace == nullptr ? -1 : workspace->bindableId().value()},
	         {"name", workspace == nullptr ? QString() : workspace->bindableName().value()},
	     }},
	    {"floating", floating},
	    {"pseudo", false},
	    {"monitor", monitor == nullptr ? -1 : monitor->bindableId().value()},
	    {"class", window->appId()},
	    {"title", window->title()},
	    {"initialClass", window->appId()},
	    {"initialTitle", window->title()},
	    {"pid", static_cast<int>(window->pid())},
	    {"xwayland", false},
	    {"pinned", VirtualDesktops::instance()->isWindowPinned(window->hwnd())},
	    {"fullscreen", fullscreen},
	    {"fullscreenClient", fullscreen},
	    {"grouped", QVariantList {}},
	    {"tags", QVariantList {}},
	    {"swallowing", "0x0"},
	    {"focusHistoryID", window->activated() ? 0 : -1},
	    {"inhibitingIdle", false},
	    {"minimized", window->minimized()},
	    {"exe", window->exePath()},
	};

	this->bLastIpcObject = object;
}

} // namespace qs::hyprland::ipc
