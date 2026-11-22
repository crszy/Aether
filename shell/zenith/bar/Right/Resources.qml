// bar/Right/Resources.qml
import ".."
import "../.."
import "../../services"
import QtQuick
import QtQuick.Layouts
import Quickshell

Item {
    id: root

    readonly property int cpu: ResourceService.cpu
    readonly property int mem: ResourceService.mem
    readonly property int temp: ResourceService.temp
    readonly property string currentProfile: PowerProfileService.currentProfile

    // Dynamic thresholds: CPU visible > 30%, RAM visible > 60%, Temp visible > 85°C, or on mouse hover
    readonly property bool isHovered: resMouse.containsMouse
    readonly property bool showCpu: cpu > 30 || isHovered
    readonly property bool showMem: mem > 60 || isHovered
    readonly property bool showTemp: temp > 85 || isHovered

    height: Theme.pillHeight
    implicitHeight: Theme.pillHeight
    Layout.preferredHeight: Theme.pillHeight
    Layout.alignment: Qt.AlignVCenter
    implicitWidth: outerContainer.width

    // Outer Glass Container matching QuickSettingsCluster exactly
    Rectangle {
        id: outerContainer
        height: Theme.pillHeight
        implicitHeight: Theme.pillHeight
        width: content.implicitWidth + Theme.scaled(24)
        implicitWidth: width
        radius: height / 2
        color: Theme.pillColor
        border.color: Theme.glassBorder
        border.width: 1
        clip: true
        scale: resMouse.pressed ? 0.95 : 1.0

        Behavior on width {
            NumberAnimation {
                duration: Theme.animNormal
                easing.type: Easing.OutCubic
            }
        }
        Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: Theme.animInstant } }

        MouseArea {
            id: resMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton)
                    QuickSettingsService.toggle("powerprofile");
            }
        }

        RowLayout {
            id: content
            anchors.centerIn: parent
            spacing: Theme.scaled(6)

            // Power Profile Cat Avatar Sub-Widget
            Item {
                width: Theme.scaled(35)
                height: Theme.scaled(35)
                Layout.alignment: Qt.AlignVCenter

                Image {
                    id: catSprite
                    anchors.centerIn: parent
                    source: "../../assets/cat_f" + Math.floor(frameTimer.frameCount % 4) + ".png"
                    width: Theme.scaled(35)
                    height: Theme.scaled(35)
                    fillMode: Image.PreserveAspectFit
                }

                Timer {
                    id: frameTimer
                    property int frameCount: 0
                    interval: {
                        switch (currentProfile) {
                            case "performance": return 80;
                            case "turbo": return 50;
                            case "powersave": return 220;
                            case "balanced": return 150;
                            default: return 200;
                        }
                    }
                    running: !HyprlandService.isFullscreen
                    repeat: true
                    onTriggered: frameCount++
                }
            }

            // Dot Separator after Cat Avatar (Visible if CPU, RAM, or Temp is shown)
            Text {
                visible: root.showCpu || root.showMem || root.showTemp
                text: "•"
                color: Theme.glassBorder
                font.pixelSize: Theme.scaled(10)
                Layout.alignment: Qt.AlignVCenter
            }

            // CPU Stats (Only visible when > 30%)
            RowLayout {
                visible: root.showCpu
                spacing: Theme.scaled(4)
                Layout.alignment: Qt.AlignVCenter

                Text {
                    text: ""
                    color: Theme.powerRed
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.iconSize
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: root.cpu + "%"
                    color: resMouse.containsMouse ? Theme.subtext0 : Theme.fontColor
                    font.pixelSize: Theme.fontSize
                    font.family: "JetBrains Mono"
                    font.weight: Font.DemiBold
                    Layout.alignment: Qt.AlignVCenter
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }
            }

            // Dot Separator before RAM (Visible ONLY if RAM is shown AND either Cat or CPU precedes it)
            Text {
                visible: root.showMem && root.showCpu
                text: "•"
                color: Theme.glassBorder
                font.pixelSize: Theme.scaled(10)
                Layout.alignment: Qt.AlignVCenter
            }

            // RAM Stats (Only visible when > 60%)
            RowLayout {
                visible: root.showMem
                spacing: Theme.scaled(4)
                Layout.alignment: Qt.AlignVCenter

                Text {
                    text: ""
                    color: Theme.powerGreen
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.iconSize
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: root.mem + "%"
                    color: resMouse.containsMouse ? Theme.subtext0 : Theme.fontColor
                    font.pixelSize: Theme.fontSize
                    font.family: "JetBrains Mono"
                    font.weight: Font.DemiBold
                    Layout.alignment: Qt.AlignVCenter
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }
            }

            // Dot Separator before Temp (Visible ONLY if Temp is shown AND preceding stat exists)
            Text {
                visible: root.showTemp && (root.showCpu || root.showMem)
                text: "•"
                color: Theme.glassBorder
                font.pixelSize: Theme.scaled(10)
                Layout.alignment: Qt.AlignVCenter
            }

            // Temp Stats (Only visible when > 90°C)
            RowLayout {
                visible: root.showTemp
                spacing: Theme.scaled(4)
                Layout.alignment: Qt.AlignVCenter

                Text {
                    text: ""
                    color: Theme.powerYellow
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.iconSize
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: root.temp + "°C"
                    color: resMouse.containsMouse ? Theme.subtext0 : Theme.fontColor
                    font.pixelSize: Theme.fontSize
                    font.family: "JetBrains Mono"
                    font.weight: Font.DemiBold
                    Layout.alignment: Qt.AlignVCenter
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }
            }
        }
    }
}
