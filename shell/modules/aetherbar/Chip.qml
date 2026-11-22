// Aether shell (AGPL-3.0-or-later). One widget's hover pill on the Aether bar: lights up under the mouse,
// squashes a little when pressed (Ryoku's RailButton feel), and passes clicks and the wheel on.
import QtQuick
import qs.services

Item {
    id: chip

    property real barH: 38
    property bool hoverable: true
    property bool active: false
    property real pad: 10
    default property alias content: row.data
    readonly property alias hovered: area.containsMouse
    signal clicked(var mouse)
    signal wheel(var wheel)

    implicitWidth: row.implicitWidth + pad * 2
    implicitHeight: barH - 8
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        z: -2
        color: chip.active ? AetherStyle.primaryContainer : AetherStyle.textMain
        opacity: chip.active ? 1 : (chip.hoverable && area.containsMouse ? (area.pressed ? 0.16 : 0.09) : 0)
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Behavior on color { ColorAnimation { duration: 200 } }
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        scale: area.pressed ? 0.94 : 1
        Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutBack; easing.overshoot: 2 } }
    }
    MouseArea {
        id: area
        // under the content: widgets with their own MouseAreas (tray icons, the tray arrow) get their clicks;
        // on top it swallowed them all and the tray did nothing (2026-11-22)
        z: -1
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        cursorShape: chip.hoverable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: m => chip.clicked(m)
        onWheel: w => chip.wheel(w)
    }
}
