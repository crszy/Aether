import "../../../services"
import "../../../Settings"
import QtQuick
import QtQuick.Controls 2.15
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland
import "../../.."

PanelWindow {
    id: osdWindow

    readonly property bool useFullscreenLayout: NotificationSettings.fullscreenOSD
    readonly property bool isFullscreen: HyprlandService.isFullscreen

    property string osdType: ""
    property real osdValue: 0

    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

    // Position (top-right)
    anchors {
        top: true
        right: true
    }

    margins {
        top: osdWindow.isFullscreen ? - (barLoader.item?.height ?? 0) : Theme.scaled(10)
        right: osdWindow.isFullscreen ? Theme.scaled(5) : Theme.scaled(10)
    }

    implicitWidth: Theme.scaled(400)
    implicitHeight: osdWindow.isFullscreen ? Theme.scaled(65) : Theme.scaled(85)
    
    // Window stays visible if timer is running OR if the mouse is hovering/pressing
    visible: !LockService.locked && (osdTimer.running || content.opacity > 0 || mainMouseArea.containsMouse) && (!osdWindow.isFullscreen || osdWindow.useFullscreenLayout)
    color: "transparent"

    Rectangle {
        id: content
        anchors.fill: parent
        color: Theme.glassBackground
        radius: Theme.scaled(20)
        border.color: Theme.glassBorder
        border.width: 1
        clip: true
        // Fade logic: Stay visible on hover
        opacity: (osdTimer.running || mainMouseArea.containsMouse) ? 1 : 0

        // This mouse area detects hover to keep the OSD alive
        MouseArea {
            id: mainMouseArea
            anchors.fill: parent
            hoverEnabled: true
            onEntered: osdTimer.stop()
            onExited: osdTimer.restart()
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: osdWindow.isFullscreen ? Theme.scaled(9) : Theme.scaled(18)
            spacing: osdWindow.isFullscreen ? Theme.scaled(4) : Theme.scaled(8)
            z: 2 // Keep content above mouse area

            RowLayout {
                Layout.fillWidth: true
                spacing: osdWindow.isFullscreen ? Theme.scaled(8) : Theme.scaled(12)

                Rectangle {
                    width: osdWindow.isFullscreen ? Theme.scaled(16) : Theme.scaled(32); 
                    height: osdWindow.isFullscreen ? Theme.scaled(16) : Theme.scaled(32); 
                    radius: osdWindow.isFullscreen ? Theme.scaled(4) : Theme.bubbleRadiusSmall; 
                    color: "transparent"
                    border.color: Theme.glassBorder
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        font.pixelSize: osdWindow.isFullscreen ? Theme.scaled(14) : Theme.scaled(18)
                        color: (osdValue <= 0) ? Theme.powerRed : (osdType === "volume" && osdValue > 1.0 ? Theme.powerYellow : Theme.powerGreen)
                        text: {
                            if (osdType === "brightness") {
                                if (osdValue <= 0.33) return "󰃞"; if (osdValue <= 0.66) return "󰃟"; return "󰃠"
                            } 
                            if (osdType === "volume") {
                                if (osdValue <= 0) return "󰝟"; if (osdValue <= 0.33) return "󰕿"; 
                                if (osdValue <= 0.66) return "󰖀"; if (osdValue <= 1.0) return "󰕾"; return "󰓃"
                            }
                            return "󰋽"
                        }
                    }
                }

                Text {
                    text: osdType.toUpperCase()
                    color: Theme.blue; font.weight: Font.Black; font.pixelSize: Theme.scaled(12); font.letterSpacing: 2
                    Layout.fillWidth: true
                }

                Text {
                    text: Math.round(osdValue * 100) + "%"
                    color: Theme.text; font.family: "JetBrains Mono"; font.weight: Font.Bold; font.pixelSize: osdWindow.isFullscreen ? Theme.scaled(11) : Theme.scaled(13)
                }
            }

            Slider {
                id: osdSlider
                Layout.fillWidth: true
                from: 0; to: 1
                value: osdWindow.osdValue
                hoverEnabled: true

                // Reset all paddings for pixel-perfect alignment
                padding: 0
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0

                readonly property real handleWidth: osdWindow.isFullscreen ? Theme.scaled(12) : Theme.scaled(14)
                
                // --- Re-enabled functionality ---
                onMoved: {
                    osdTimer.restart();
                    osdWindow.osdValue = value;
                    NotificationService.updateOSDValue(osdWindow.osdType, value);
                }

                background: Rectangle {
                    x: osdSlider.leftPadding + osdSlider.handleWidth / 1
                    y: osdWindow.isFullscreen ? osdSlider.topPadding + (osdSlider.availableHeight - height) / 5 : osdSlider.topPadding + (osdSlider.availableHeight - height) / 2
                    implicitHeight: osdWindow.isFullscreen ? Theme.scaled(8) : Theme.scaled(10)
                    width: osdSlider.availableWidth - osdSlider.handleWidth
                    radius: Theme.scaled(3); color: Qt.rgba(255, 255, 255, 0.08); border.color: Theme.glassBorder; border.width: 1
                    Rectangle {
                        width: osdSlider.visualPosition * parent.width
                        height: osdWindow.isFullscreen ? Theme.scaled(12) : parent.height
                        color: osdValue > 1.0 ? Theme.powerYellow : Theme.powerGreen
                        radius: Theme.scaled(30)
                    }
                }

                handle: Rectangle {
                    x: osdSlider.leftPadding + osdSlider.visualPosition * (osdSlider.availableWidth - width)
                    y: osdSlider.topPadding + (osdSlider.availableHeight - height) / 2
                    implicitWidth: osdSlider.handleWidth; implicitHeight: osdWindow.isFullscreen ? Theme.scaled(12) : Theme.scaled(14); radius: width / 2
                    color: Theme.text; border.color: Theme.glassBorder; border.width: 1

                    scale: osdSlider.pressed ? 1.05 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
                }
            }
        }

        Behavior on opacity { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
    }

    Timer {
        id: osdTimer
        interval: 2500 // Increased slightly for comfort
    }

    Connections {
        target: NotificationService
        function onOsdReceived(type, value) {
            osdWindow.osdType = type
            osdWindow.osdValue = value
            osdTimer.restart()
        }
    }
}
