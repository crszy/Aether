pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.Common
import qs.dms.Modules.Plugins

BasePill {
    id: root
    property var widgetData: null
    property var surfaceContext: null
    enableBackgroundHover: true
    MediaActivitySource { id: source }
    content: Component {
        MediaActivityFace {
            mediaModel: source
            dense: true
            isVertical: root.isVerticalOrientation
            contentScale: root.surfaceContext?.kind === "dock" ? root.widgetThickness / 40 : 1
            artworkSize: Math.min(root.widgetThickness, Theme.iconSizeLarge * contentScale)
            enabled: root.surfaceLive
            onClockClicked: root.clicked()
        }
    }
}
