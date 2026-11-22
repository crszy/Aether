// bar/Right/QuickSettingsCluster.qml
import ".."
import "../.."
import "../../services"
import "../../Settings"
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Hyprland
import Quickshell.Io

Item {
    id: root

    height: Theme.pillHeight
    implicitHeight: Theme.pillHeight
    Layout.preferredHeight: Theme.pillHeight
    Layout.alignment: Qt.AlignVCenter
    implicitWidth: outerContainer.implicitWidth

    // Helper functions matching original widgets
    function volumeIcon(v, m) {
        if (m) return Theme.volMute;
        if (v >= 70) return Theme.volHigh;
        if (v >= 30) return Theme.volMid;
        return Theme.volLow;
    }

    readonly property int batPercent: Math.max(0, Math.min(100, BatteryService.percentage))
    readonly property string batState: BatteryService.status
    readonly property bool acOnline: BatteryService.acOnline
    readonly property color batFillCol: batPercent <= 15 ? "#ef4444" : Qt.rgba(1, 1, 1, 0.22)
    readonly property color batBorderCol: batPercent <= 15 ? "#ef4444" : "#ffffff"

    function batteryIcon(p, state, ac) {
        const isLimitActive = (state === "not charging" || state === "full" || state === "idle") && ac;
        if (isLimitActive) return "";
        if (state === "charging") return "󰂄";
        if (p >= 90) return "󰁹";
        if (p >= 75) return "󰂁";
        if (p >= 60) return "󰁿";
        if (p >= 40) return "󰁽";
        if (p >= 20) return "󰁻";
        return "󰂎";
    }

    function batteryColor(p, state, ac) {
        if (state === "charging") return Theme.powerGreen;
        if (p <= 15) return Theme.red;
        return Theme.fontColor;
    }

    // Outer Glass Container (No line spacers, clean rounded pills layout)
    Rectangle {
        id: outerContainer
        height: Theme.pillHeight
        implicitHeight: Theme.pillHeight
        width: clusterRow.implicitWidth + Theme.scaled(8) + Theme.scaled(14)
        implicitWidth: width
        radius: height / 2
        color: Theme.pillColor
        border.color: Theme.glassBorder
        border.width: 1
        clip: true

        Behavior on width {
            NumberAnimation {
                duration: Theme.animNormal
                easing.type: Easing.OutCubic
            }
        }

        RowLayout {
            id: clusterRow
            anchors.left: parent.left
            anchors.leftMargin: Theme.scaled(8)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.scaled(4)

            // ================= MICROPHONE SUB-WIDGET =================
            Item {
                id: micSubBtn
                height: Theme.scaled(28)
                implicitHeight: Theme.scaled(28)
                implicitWidth: micLayout.implicitWidth + Theme.scaled(12)
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: implicitHeight
                Layout.alignment: Qt.AlignVCenter
                visible: VolumeService.micActive

                scale: micMouse.pressed ? 0.94 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                RowLayout {
                    id: micLayout
                    anchors.centerIn: parent
                    spacing: Theme.scaled(4)

                    Text {
                        text: VolumeService.micMuted ? "\uf131" : "\uf130"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(Theme.iconSize + 1)
                        color: micMouse.containsMouse
                            ? (VolumeService.micMuted ? Qt.rgba(Theme.red.r, Theme.red.g, Theme.red.b, 0.65) : Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.65))
                            : (VolumeService.micMuted ? Theme.red : Theme.accentColor)
                        Layout.alignment: Qt.AlignVCenter
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    }
                }

                MouseArea {
                    id: micMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton
                    onClicked: (mouse) => {
                        micMuteExec.running = false;
                        micMuteExec.running = true;
                        VolumeService.update();
                    }
                }
            }

            // ================= VOLUME SUB-WIDGET =================
            Item {
                id: volSubBtn
                height: Theme.scaled(28)
                implicitHeight: Theme.scaled(28)
                implicitWidth: volLayout.implicitWidth + Theme.scaled(10)
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: implicitHeight
                Layout.alignment: Qt.AlignVCenter

                property bool showVolText: false

                Timer {
                    id: volTextTimer
                    interval: 3000
                    onTriggered: volSubBtn.showVolText = false
                }

                Connections {
                    target: VolumeService
                    function onOutputVolumeChanged() {
                        volSubBtn.showVolText = true;
                        volTextTimer.restart();
                    }
                    function onMutedChanged() {
                        volSubBtn.showVolText = true;
                        volTextTimer.restart();
                    }
                }

                scale: volMouse.pressed ? 0.94 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                RowLayout {
                    id: volLayout
                    anchors.centerIn: parent
                    spacing: Theme.scaled(4)

                    // Speaker Icon
                    Text {
                        text: root.volumeIcon(VolumeService.outputVolume, VolumeService.muted)
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(Theme.iconSize + 1)
                        color: volMouse.containsMouse
                            ? Theme.subtext0
                            : (VolumeService.btActive ? Theme.bluetoothColor : Theme.fontColor)
                        Layout.alignment: Qt.AlignVCenter
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    }

                    // Percentage Text (Visible ONLY when volume changes or mouse hovers, hides after 3s)
                    Text {
                        visible: volSubBtn.showVolText || volMouse.containsMouse
                        text: VolumeService.muted ? "Muted" : VolumeService.outputVolume + "%"
                        font.pixelSize: Theme.fontSize
                        font.family: "JetBrains Mono"
                        color: volMouse.containsMouse
                            ? Theme.subtext0
                            : (VolumeService.btActive ? Theme.bluetoothColor : Theme.fontColor)
                        Layout.alignment: Qt.AlignVCenter
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    }
                }

                MouseArea {
                    id: volMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            muteExec.running = false;
                            muteExec.running = true;
                        } else if (mouse.button === Qt.LeftButton) {
                            QuickSettingsService.toggle("volume");
                        }
                    }

                    onWheel: (wheel) => {
                        if (wheel.angleDelta.y > 0) {
                            volUp.running = false;
                            volUp.running = true;
                        } else {
                            volDown.running = false;
                            volDown.running = true;
                        }
                        VolumeService.update();
                        volSubBtn.showVolText = true;
                        volTextTimer.restart();
                    }
                }
            }

            // ================= BLUETOOTH SUB-WIDGET =================
            Item {
                id: btSubBtn
                height: Theme.scaled(28)
                implicitHeight: Theme.scaled(28)
                implicitWidth: btLayout.implicitWidth + Theme.scaled(10)
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: implicitHeight
                Layout.alignment: Qt.AlignVCenter
                // Gone entirely on a machine with no adapter, rather than
                // sitting there permanently off.
                visible: HardwareService.hasBluetooth && BluetoothService.powered

                scale: btMouse.pressed ? 0.94 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                RowLayout {
                    id: btLayout
                    anchors.centerIn: parent
                    spacing: Theme.scaled(4)

                    Text {
                        text: BluetoothService.powered ? Theme.btIcon : "󰂲"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(Theme.iconSize + 1)
                        color: btMouse.containsMouse
                            ? Theme.subtext0
                            : (BluetoothService.connected ? Theme.bluetoothColor : (BluetoothService.powered ? Theme.fontColor : Theme.inactiveTextColor))
                        Layout.alignment: Qt.AlignVCenter
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    }
                }

                MouseArea {
                    id: btMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton
                    onClicked: (mouse) => {
                        BluetoothService.refresh();
                        QuickSettingsService.toggle("bluetooth");
                    }
                }
            }

            // ================= BATTERY SUB-WIDGET =================
            Item {
                id: batSubBtn
                height: Theme.scaled(28)
                implicitHeight: Theme.scaled(28)
                implicitWidth: batLayout.implicitWidth + Theme.scaled(10)
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: implicitHeight
                Layout.alignment: Qt.AlignVCenter
                // A desktop or VM has no battery to report, so the readout
                // is removed rather than showing a meaningless 0%.
                visible: HardwareService.hasBattery && WidgetSettings.enableBattery
                         && !HyprlandService.isFullscreen

                scale: batMouse.pressed ? 0.94 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                RowLayout {
                    id: batLayout
                    anchors.centerIn: parent
                    spacing: Theme.scaled(4)
                    opacity: batMouse.containsMouse ? 0.70 : 1.0
                    Behavior on opacity { NumberAnimation { duration: Theme.animFast } }

                    // Vertical Battery Icon matching other cluster icons
                    Text {
                        text: root.batteryIcon(root.batPercent, root.batState, root.acOnline)
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(Theme.iconSize + 1)
                        color: root.batteryColor(root.batPercent, root.batState, root.acOnline)
                        Layout.alignment: Qt.AlignVCenter

                        SequentialAnimation on opacity {
                            running: root.batState === "charging"
                            loops: Animation.Infinite
                            NumberAnimation { from: 1.0; to: 0.4; duration: 500; easing.type: Easing.InOutQuad }
                            NumberAnimation { from: 0.4; to: 1.0; duration: 500; easing.type: Easing.InOutQuad }
                        }
                    }

                    // Percentage Text (Shown ONLY when battery <= 20% or on mouse hover)
                    Text {
                        visible: root.batPercent <= 20 || batMouse.containsMouse
                        text: (root.batPercent >= 0 ? root.batPercent : 0) + "%"
                        font.pixelSize: Theme.fontSize
                        font.family: "JetBrains Mono"
                        font.weight: Font.Bold
                        color: root.batteryColor(root.batPercent, root.batState, root.acOnline)
                        Layout.alignment: Qt.AlignVCenter
                    }
                }

                MouseArea {
                    id: batMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton
                    onClicked: {
                        QuickSettingsService.toggle("battery");
                    }
                }
            }

            // ================= POWER SUB-WIDGET =================
            Item {
                id: pwrSubBtn
                height: Theme.scaled(28)
                implicitHeight: Theme.scaled(28)
                implicitWidth: Theme.scaled(24)
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: implicitHeight
                Layout.alignment: Qt.AlignVCenter

                scale: pwrMouse.pressed ? 0.94 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                Text {
                    anchors.centerIn: parent
                    text: ""
                    color: pwrMouse.containsMouse ? Theme.subtext0 : Theme.fontColor
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.scaled(Theme.iconSize + 1)
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter

                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }

                MouseArea {
                    id: pwrMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.LeftButton) {
                            QuickSettingsService.toggle("power");
                        } else if (mouse.button === Qt.RightButton) {
                            powerExec.running = false;
                            powerExec.running = true;
                        }
                    }
                }
            }
        }
    }

    // Audio & Power Processes
    Process {
        id: micMuteExec
        command: ["wpctl", "set-mute", "@DEFAULT_AUDIO_SOURCE@", "toggle"]
    }

    Process {
        id: muteExec
        command: ["wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"]
    }

    Process {
        id: volUp
        command: ["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "5%+"]
    }

    Process {
        id: volDown
        command: ["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "5%-"]
    }

    Process {
        id: powerExec
        command: ["wlogout"]
    }
}
