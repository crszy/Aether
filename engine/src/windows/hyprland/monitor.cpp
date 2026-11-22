#include "monitor.hpp"
#include <cmath>

#include <qcontainerfwd.h>
#include <qobject.h>
#include <qproperty.h>
#include <qscreen.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../util.hpp"
#include "connection.hpp"
#include "workspace.hpp"

namespace qs::hyprland::ipc {

HyprlandMonitor::HyprlandMonitor(HyprlandIpc* ipc, QScreen* screen)
    : QObject(ipc)
    , ipc(ipc)
    , mScreen(screen) {
	this->restoreBindings();

	QObject::connect(screen, &QScreen::geometryChanged, this, [this]() {
		this->updateFromScreen(this->bId.value());
	});

	QObject::connect(screen, &QScreen::availableGeometryChanged, this, &HyprlandMonitor::refreshIpcObject);
	QObject::connect(this, &HyprlandMonitor::activeWorkspaceChanged, this, &HyprlandMonitor::refreshIpcObject);
	QObject::connect(this, &HyprlandMonitor::focusedChanged, this, &HyprlandMonitor::refreshIpcObject);
}

void HyprlandMonitor::updateFromScreen(qint32 id) {
	auto geometry = this->mScreen->geometry();
	auto dpr = this->mScreen->devicePixelRatio();

	Qt::beginPropertyUpdateGroup();
	this->bId = id;
	this->bName = this->mScreen->name();
	this->bDescription = (this->mScreen->manufacturer() + " " + this->mScreen->model()).trimmed();
	this->bX = geometry.x();
	this->bY = geometry.y();
	this->bWidth = static_cast<qint32>(std::lround(geometry.width() * dpr));
	this->bHeight = static_cast<qint32>(std::lround(geometry.height() * dpr));
	this->bScale = dpr;
	Qt::endPropertyUpdateGroup();

	this->refreshIpcObject();
}

void HyprlandMonitor::refreshIpcObject() {
	auto dpr = this->mScreen->devicePixelRatio();
	auto rects = qs::windows::monitorRects(qs::windows::monitorForScreen(this->mScreen));

	auto reserved = QVariantList {0, 0, 0, 0};
	if (rects.valid) {
		auto logical = [dpr](int physical) { return static_cast<int>(std::lround(physical / dpr)); };
		reserved = QVariantList {
		    logical(rects.work.left() - rects.monitor.left()),
		    logical(rects.work.top() - rects.monitor.top()),
		    logical(rects.monitor.right() - rects.work.right()),
		    logical(rects.monitor.bottom() - rects.work.bottom()),
		};
	}

	auto* workspace = this->bActiveWorkspace.value();

	QVariantMap object {
	    {"id", this->bId.value()},
	    {"name", this->bName.value()},
	    {"description", this->bDescription.value()},
	    {"make", this->mScreen->manufacturer()},
	    {"model", this->mScreen->model()},
	    {"serial", this->mScreen->serialNumber()},
	    {"width", this->bWidth.value()},
	    {"height", this->bHeight.value()},
	    {"refreshRate", this->mScreen->refreshRate()},
	    {"x", this->bX.value()},
	    {"y", this->bY.value()},
	    {"activeWorkspace",
	     QVariantMap {
	         {"id", workspace == nullptr ? -1 : workspace->bindableId().value()},
	         {"name", workspace == nullptr ? QString() : workspace->bindableName().value()},
	     }},
	    {"specialWorkspace", QVariantMap {{"id", 0}, {"name", QString()}}},
	    {"reserved", reserved},
	    {"scale", this->bScale.value()},
	    {"transform", 0},
	    {"focused", this->bFocused.value()},
	    {"dpmsStatus", true},
	    {"vrr", false},
	    {"activelyTearing", false},
	    {"disabled", false},
	    {"currentFormat", QString()},
	    {"availableModes", QVariantList {}},
	};

	if (object == this->mLastIpcObject) return;
	this->mLastIpcObject = object;
	emit this->lastIpcObjectChanged();
}

void HyprlandMonitor::restoreBindings() {
	this->bActiveWorkspace.setBinding([this]() { return this->ipc->bindableFocusedWorkspace().value(); });
	this->bFocused.setBinding([this]() { return this->ipc->bindableFocusedMonitor().value() == this; });
}

void HyprlandMonitor::setWmActiveWorkspace(HyprlandWorkspace* workspace) {
	if (this->bActiveWorkspace.hasBinding() || this->bActiveWorkspace.value() != workspace) {
		this->bActiveWorkspace = workspace;
	}
}

} // namespace qs::hyprland::ipc
