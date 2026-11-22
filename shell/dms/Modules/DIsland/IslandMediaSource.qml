pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.Modules.SurfaceWidgets

MediaActivitySource {
    required property IslandController controller
    Component.onCompleted: controller.updateMediaAvailability(available)
    onAvailableChanged: controller.updateMediaAvailability(available)
}
