import QtQuick
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.waffle.looks
import qs.end4.modules.waffle.bar

BarButton {
    id: root

    property alias iconName: iconContent.icon
    property alias iconSource: iconContent.source
    property alias iconSize: iconContent.implicitSize
    property alias iconRotation: iconContent.rotation
    property alias iconMonochrome: iconContent.monochrome
    property alias iconScale: iconContent.scale
    property alias tooltipText: tooltip.text
    property alias overlayingItems: iconContent.data

    implicitWidth: 32

    contentItem: Item {
        anchors.centerIn: parent
        implicitWidth: iconContent.implicitWidth
        implicitHeight: iconContent.implicitHeight

        FluentIcon {
            id: iconContent
            anchors.centerIn: parent
            implicitSize: 16
            icon: root.iconName
            monochrome: false
        }
    }

    BarToolTip {
        id: tooltip
        extraVisibleCondition: root.shouldShowTooltip && text !== ""
    }
}
