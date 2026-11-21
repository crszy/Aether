import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Wayland
import Quickshell.Hyprland
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.widgets
import qs.end4.modules.waffle.looks
import qs.end4.modules.waffle.notificationCenter

Scope {
    id: notificationPopup

    PanelWindow {
        id: root
        visible: (Notifications.popupList.length > 0) && !GlobalStates.screenLocked
        screen: Quickshell.screens.find(s => s.name === Hyprland.focusedMonitor?.name) ?? null

        WlrLayershell.namespace: "quickshell:notificationPopup"
        WlrLayershell.layer: WlrLayer.Overlay
        exclusiveZone: 0

        anchors {
            top: true
            right: true
            bottom: true
        }

        mask: Region {
            item: listview.contentItem
        }

        color: "transparent"
        implicitWidth: listview.implicitWidth

        WListView {
            id: listview
            anchors {
                bottom: parent.bottom
                right: parent.right
                left: parent.left
            }
            leftMargin: 16
            rightMargin: 16
            topMargin: 16
            bottomMargin: 16

            height: Math.min(contentItem.height + topMargin + bottomMargin, parent.height)
            width: parent.width - Appearance.sizes.elevationMargin * 2
            
            implicitWidth: 396
            spacing:12

            model: ScriptModel {
                values: Notifications.popupList
            }
            delegate: WSingleNotification {
                required property var modelData
                notification: modelData
                width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
            }
        }
    }
}
