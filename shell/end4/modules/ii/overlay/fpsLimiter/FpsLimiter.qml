import QtQuick
import Quickshell
import qs.end4.modules.common
import qs.end4.modules.ii.overlay

StyledOverlayWidget {
    id: root
    title: "MangoHud FPS"
    minimumWidth: 275
    minimumHeight: 100
    contentItem: FpsLimiterContent {
        radius: root.contentRadius
    }
}
