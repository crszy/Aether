import QtQuick
import Qt5Compat.GraphicalEffects
import qs.end4
import qs.end4.services
import qs.end4.services.network
import qs.end4.modules.common
import qs.end4.modules.common.widgets
import qs.end4.modules.waffle.looks

StyledIndeterminateProgressBar {
    id: progressBar
    implicitHeight: 3
    background: null
    layer.enabled: true
    layer.effect: OpacityMask {
        maskSource: Rectangle {
            width: progressBar.width
            height: progressBar.height
            radius: progressBar.height / 2
        }
    }
}
