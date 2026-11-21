pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash

DashTabFace {
    id: root

    activityId: "home"
    entryId: DashRegistry.overviewId
    tabComponent: Component {
        OverviewTab {
            live: root.live
            editMode: root.editMode
            rowBudget: root.controller.dashboardRowBudget
            transientSurfaceTracker: root.controller.transientSurfaces
            onTabRequested: id => root.controller.requestDashTab(id)
        }
    }
}
