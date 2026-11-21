pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.Modules.DDash

DashTabFace {
    id: root

    activityId: "media"
    tabComponent: Component {
        MediaPlayerTab {
            live: root.live
        }
    }
}
