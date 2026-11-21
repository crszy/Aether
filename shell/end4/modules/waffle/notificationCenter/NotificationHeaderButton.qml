import QtQuick
import QtQuick.Layouts
import Quickshell
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.widgets
import qs.end4.modules.common.functions
import qs.end4.modules.waffle.looks

WBorderlessButton {
    id: root
    Layout.fillWidth: false
    property real implicitSize: 16
    implicitWidth: implicitSize
    implicitHeight: implicitSize
    color: "transparent"
    colForeground: root.hovered && !root.pressed ? Looks.colors.fg : Looks.colors.fg1

    Behavior on colForeground {
        animation: Looks.transition.color.createObject(this)
    }

    contentItem: Item {
        FluentIcon {
            anchors.centerIn: parent
            implicitSize: root.implicitSize
            icon: root.icon.name
            color: root.colForeground
        }
    }
}
