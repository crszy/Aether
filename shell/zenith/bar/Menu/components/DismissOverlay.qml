import QtQuick
import Quickshell
import Quickshell.Wayland
import "../../../services"

PanelWindow {
    id: root

    // Cover the whole screen
    implicitWidth: screen ? screen.width : 1920
    implicitHeight: screen ? screen.height : 1080

    WlrLayershell.layer: WlrLayer.Overlay
    exclusiveZone: 0
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    WlrLayershell.namespace: "dismiss_overlay"
    anchors.top: true
    anchors.bottom: true
    anchors.left: true
    anchors.right: true
    color: "transparent"
    visible: MenuService.openMenus.length > 0



    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        
        onPressed: MenuService.closeAll()
    }
}
