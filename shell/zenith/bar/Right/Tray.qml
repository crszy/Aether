// bar/Right/Tray.qml
import "../.."
import QtQuick
import QtQuick.Layouts
import Quickshell.Services.SystemTray

Rectangle {
    id: trayContainer

    property var menuRef

    // Nothing to show, nothing to draw. SystemTray.items is an ObjectModel:
    // it has no `length`, so the old `SystemTray.items.length === 0` test was
    // `undefined === 0` -- always false -- which is why the empty-state icon
    // below it never appeared and the pill just sat there empty.
    readonly property int itemCount: SystemTray.items.values.length
    visible: itemCount > 0

    height: Theme.pillHeight
    implicitHeight: Theme.pillHeight
    Layout.preferredHeight: Theme.pillHeight
    Layout.alignment: Qt.AlignVCenter
    width: trayRow.implicitWidth + Theme.pillPadding + Theme.extraPillPadding
    implicitWidth: width
    color: Theme.pillColor
    radius: Theme.pillRadius
    border.color: Theme.glassBorder
    border.width: 1
    scale: 1.0

    Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
    Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
    Behavior on color { ColorAnimation { duration: Theme.animInstant } }

    MouseArea {
        id: trayHoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    RowLayout {
        id: trayRow
        anchors.centerIn: parent
        spacing: Theme.scaled(4)

        Repeater {
            model: SystemTray.items

            delegate: TrayItem {
                item: modelData
                menuRef: trayContainer.menuRef
            }
        }
    }
}
