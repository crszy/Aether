// bar/Right/Network.qml
import ".."
import "../.."
import "../../services"
import QtQuick
import QtQuick.Layouts
import Quickshell

Item {
    id: root

    property var menuRef
    property bool showUpload: false
    // Measured by the resources daemon, which already samples /proc every
    // 1.5s; this widget used to spawn its own awk on a timer for the same data.
    readonly property int downSpeed: ResourceService.netDown
    readonly property int upSpeed: ResourceService.netUp

    readonly property bool wifiConnected: WifiService.currentState === "connected"
    readonly property string wifiSSID: WifiService.currentSsid
    readonly property bool airplaneMode: HardwareService.hasWifi && WifiService.isAirplane

    // A machine with no wifi adapter -- a desktop, or most VMs -- is not
    // "disconnected" just because there is no SSID. It is on ethernet, and the
    // widget showed DISC over a perfectly working network.
    readonly property bool wiredOnly: !HardwareService.hasWifi && HardwareService.hasEthernet
    readonly property bool wiredUp: wiredOnly && WifiService.ipv4Address !== ""
    readonly property bool online: wifiConnected || wiredUp

    function formatSpeed(kb) {
        if (kb < 1024)
            return kb + " KB/s";
        return (kb / 1024).toFixed(1) + " MB/s";
    }

    height: Theme.pillHeight
    implicitHeight: Theme.pillHeight
    Layout.preferredHeight: Theme.pillHeight
    Layout.alignment: Qt.AlignVCenter
    implicitWidth: outerContainer.width

    // Outer Glass Container matching Resources & QuickSettingsCluster exactly
    Rectangle {
        id: outerContainer
        height: Theme.pillHeight
        implicitHeight: Theme.pillHeight
        width: content.implicitWidth + Theme.scaled(20)
        implicitWidth: width
        radius: height / 2
        color: Theme.pillColor
        border.color: Theme.glassBorder
        scale: netMouse.pressed ? 0.95 : 1.0

        Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: Theme.animInstant } }

        MouseArea {
            id: netMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton)
                    showUpload = !showUpload;
                else if (mouse.button === Qt.LeftButton)
                    QuickSettingsService.toggle("network"); 
            }
        }

        RowLayout {
            id: content
            anchors.centerIn: parent
            spacing: Theme.scaled(6)

            Text {
                text: airplaneMode ? "󰀞" : (!online ? "󰤮" : (showUpload ? Theme.netUpIcon : Theme.netDownIcon))
                font.family: Theme.iconFont
                font.pixelSize: Theme.iconSize
                color: airplaneMode ? Theme.powerRed : (!wifiConnected ? Theme.powerRed : (netMouse.containsMouse ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.65) : Theme.accentColor))
                Layout.alignment: Qt.AlignVCenter
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
            }

            Text {
                text: airplaneMode ? "OFF" : (!online ? "DISC" : (netMouse.containsMouse ? (root.wiredOnly ? "Wired" : (wifiSSID ? wifiSSID : "Connected")) : formatSpeed(showUpload ? upSpeed : downSpeed)))
                font.pixelSize: Theme.fontSize
                font.family: "JetBrains Mono"
                font.weight: (airplaneMode || !wifiConnected) ? Font.Bold : Font.DemiBold
                color: airplaneMode ? Theme.powerRed : (!wifiConnected ? Theme.powerRed : (netMouse.containsMouse ? Theme.subtext0 : Theme.fontColor))
                Layout.alignment: Qt.AlignVCenter
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
            }
        }
    }
}
