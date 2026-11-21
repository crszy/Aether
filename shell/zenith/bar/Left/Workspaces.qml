import ".."
import "../.."
import "../../Settings"
import "../../services"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import Quickshell.Hyprland

Item {
    id: workspaceBar

    height: Theme.pillHeight
    implicitHeight: Theme.pillHeight
    Layout.preferredHeight: Theme.pillHeight
    Layout.alignment: Qt.AlignVCenter
    implicitWidth: mainPill.width

    readonly property HyprlandMonitor monitor: (QsWindow.window && QsWindow.window.monitor) ? QsWindow.window.monitor : null

    // Floating Bubble Pill Container
    Rectangle {
        id: mainPill
        anchors.centerIn: parent
        height: Theme.pillHeight
        implicitHeight: Theme.pillHeight
        width: row.implicitWidth + Theme.scaled(20)
        color: pillHoverArea.containsMouse ? Theme.pillHoverColor : Theme.pillColor
        radius: height / 2

        Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
        Behavior on color { ColorAnimation { duration: Theme.animFast } }

        MouseArea {
            id: pillHoverArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }

        Row {
            id: row
            anchors.centerIn: parent
            spacing: Theme.scaled(8)

            Repeater {
                model: Hyprland.workspaces

                delegate: Rectangle {
                    id: wsBubble

                    visible: modelData && modelData.id > 0

                    readonly property bool isOccupied: modelData ? (modelData.toplevels.count > 0) : false
                    readonly property bool isCurrentActive: (Hyprland.focusedWorkspace && modelData) ? (Hyprland.focusedWorkspace.id === modelData.id) : false

                    // Bubble & Ball Dimensions:
                    // Active = Expanded Pill Bubble (28-34dp width, 22dp height)
                    // Occupied = Circular Ball Bubble (18dp diameter)
                    // Empty = Small Ball Bubble (12dp diameter)
                    height: isCurrentActive ? Theme.scaled(22) : (isOccupied ? Theme.scaled(18) : Theme.scaled(12))
                    width: isCurrentActive ? (WorkspaceSettings.displayStyle === "numbers" ? Theme.scaled(34) : Theme.scaled(28)) : height
                    radius: height / 2
                    anchors.verticalCenter: parent.verticalCenter

                    color: isCurrentActive
                           ? Theme.accentColor
                           : (isOccupied ? Theme.surfaceContainerHigh : Qt.rgba(255, 255, 255, 0.12))

                    border.width: isCurrentActive ? 0 : 1
                    border.color: isCurrentActive ? "transparent" : (isOccupied ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.35) : Theme.glassBorder)

                    // Bouncy Morphing Animations
                    Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
                    Behavior on height { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    Behavior on radius { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }

                    // Elastic Bubble Bounce on Hover & Click
                    scale: wsMouse.pressed ? 0.8 : (wsMouse.containsMouse ? 1.25 : 1.0)
                    Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Theme.elasticEasing; easing.overshoot: 1.15 } }

                    MouseArea {
                        id: wsMouse
                        anchors.fill: parent
                        anchors.margins: -Theme.scaled(4)
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData) {
                                if (typeof modelData.activate === "function") {
                                    modelData.activate();
                                } else {
                                    Hyprland.dispatch("workspace " + modelData.id);
                                }
                            }
                        }
                    }

                    // Inner Glow Sphere for Active / Occupied Balls
                    Rectangle {
                        anchors.centerIn: parent
                        width: isCurrentActive ? 0 : (isOccupied ? Theme.scaled(6) : 0)
                        height: width
                        radius: width / 2
                        color: Theme.accentColor
                        opacity: isOccupied ? 0.9 : 0.0

                        Behavior on opacity { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
                        Behavior on width { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
                    }

                    // Workspace Number Text for Active Bubble
                    Text {
                        anchors.centerIn: parent
                        text: modelData ? modelData.id.toString() : ""
                        font.pixelSize: Theme.scaled(10)
                        font.weight: Font.Black
                        color: Colors.on_primary
                        visible: isCurrentActive && WorkspaceSettings.displayStyle === "numbers"
                        opacity: isCurrentActive ? 1.0 : 0.0

                        Behavior on opacity { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
                    }
                }
            }
        }
    }

    WheelHandler {
        onWheel: (event) => {
            if (event.angleDelta.y < 0) {
                Hyprland.dispatch("workspace", "e+1");
            } else if (event.angleDelta.y > 0) {
                Hyprland.dispatch("workspace", "e-1");
            }
        }
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    }
}