import "../.."
import "../../../"
import "../../../services"
import QtQuick
import QtQuick.Layouts
import Quickshell

ColumnLayout {
    id: root
    spacing: Theme.scaled(25)
    Layout.fillWidth: true

    opacity: 0
    scale: 0.98
    Component.onCompleted: {
        entryAnim.start();
    }
    ParallelAnimation {
        id: entryAnim
        NumberAnimation { target: root; property: "opacity"; to: 1; duration: 400; easing.type: Easing.OutCubic }
        NumberAnimation { target: root; property: "scale"; to: 1; duration: 500; easing.type: Theme.elasticEasing }
    }

    readonly property bool isLimitActive: (BatteryService.status === "not charging" || BatteryService.status === "full") && BatteryService.acOnline
    readonly property real displayVoltage: BatteryService.voltage > 1000 ? BatteryService.voltage / 1000000 : BatteryService.voltage
    readonly property real displayWatts: BatteryService.energyRate > 1000 ? BatteryService.energyRate / 1000000 : BatteryService.energyRate

    Text {
        text: "BATTERY"
        color: Theme.blue
        font.pixelSize: 10
        font.weight: Font.Black
        font.letterSpacing: 2
        Layout.leftMargin: Theme.scaled(5)
    }

    // Main Card
    Rectangle {
        Layout.fillWidth: true
        height: Theme.scaled(140)
        color: Theme.cardBackground
        radius: Theme.bubbleRadiusMedium
        border.color: Theme.glassBorder
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.scaled(20)
            spacing: Theme.scaled(25)

            Rectangle {
                width: Theme.scaled(80); height: Theme.scaled(80); radius: 40
                color: "transparent"
                border.width: 0
                Text {
                    anchors.centerIn: parent
                    text: BatteryService.acOnline ? "󱐋" : "󰁹"
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.scaled(38)
                    color: BatteryService.acOnline ? Theme.powerGreen : Theme.accentColor
                }
            }

            ColumnLayout {
                spacing: 5; Layout.fillWidth: true
                RowLayout {
                    spacing: 15
                    Text { text: BatteryService.percentage + "%"; font.pixelSize: Theme.scaled(38); font.weight: Font.Black; color: Theme.text }
                    Rectangle {
                        height: 22; width: pillText.implicitWidth + 30; radius: 11
                        color: BatteryService.acOnline ? Theme.powerGreen : Theme.menuDarkHover
                        border.width: 0
                        Text { id: pillText; anchors.centerIn: parent; text: BatteryService.acOnline ? "PLUGGED" : "DISCHARGING"; color: BatteryService.acOnline ? Colors.background : Theme.text; font.weight: Font.Black; font.pixelSize: 9 }
                    }
                }
                Text { text: (root.isLimitActive ? "Conservative" : BatteryService.status).toUpperCase(); font.pixelSize: 10; font.weight: Font.Black; color: Theme.subtext0 }
                
                Rectangle {
                    Layout.fillWidth: true; Layout.topMargin: 10; height: 8; radius: 4; color: Qt.rgba(1,1,1,0.08)
                    Rectangle {
                        width: parent.width * (BatteryService.percentage / 100); height: parent.height; radius: 4
                        color: BatteryService.acOnline ? Theme.powerGreen : (BatteryService.percentage <= 20 ? Theme.red : Theme.accentColor)
                        Behavior on width { NumberAnimation { duration: 1000; easing.type: Easing.OutCubic } }
                    }
                }
            }
        }
    }

    // Info Grid
    RowLayout {
        Layout.fillWidth: true; spacing: 15
        StatCard { label: "CYCLES"; value: BatteryService.cycleCount; icon: "󱂇"; accent: Theme.mauve }
        StatCard { label: "REMAINING"; value: root.isLimitActive ? "N/A" : (BatteryService.timeRemaining || "..."); icon: "󰥔"; accent: Theme.accentColor }
    }

    component StatCard: Rectangle {
        property string label; property var value; property string icon; property color accent
        Layout.fillWidth: true; height: 90; color: Theme.cardBackground; radius: Theme.bubbleRadiusMedium; border.color: Theme.glassBorder; border.width: 1
        ColumnLayout {
            anchors.centerIn: parent; spacing: 5
            Text { text: icon; font.family: Theme.iconFont; font.pixelSize: 28; color: accent; Layout.alignment: Qt.AlignHCenter }
            Text { text: value; color: Theme.text; font.weight: Font.Black; font.pixelSize: 14; Layout.alignment: Qt.AlignHCenter }
            Text { text: label; color: Theme.subtext0; font.pixelSize: 8; font.weight: Font.Black; Layout.alignment: Qt.AlignHCenter }
        }
    }

    // Bottom Detailed Grid
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.scaled(60) 
        color: Theme.cardBackground
        radius: Theme.bubbleRadiusMedium
        border.color: Theme.glassBorder
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.scaled(20)
            anchors.rightMargin: Theme.scaled(20)
            spacing: 0

            RowLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.scaled(8)
                Text { text: "Voltage:"; color: Theme.subtext1; font.pixelSize: Theme.scaled(13) }
                Text { text: root.displayVoltage.toFixed(1) + "V"; color: Theme.text; font.bold: true; font.pixelSize: Theme.scaled(16) }
            }

            Item { Layout.fillWidth: true }

            RowLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.scaled(8)
                Text { text: "Wattage:"; color: Theme.subtext1; font.pixelSize: Theme.scaled(13) }
                Text { text: root.displayWatts.toFixed(1) + "W"; color: Theme.text; font.bold: true; font.pixelSize: Theme.scaled(16) }
            }

            Item { Layout.fillWidth: true }

            RowLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.scaled(8)
                Text { text: "Health:"; color: Theme.subtext1; font.pixelSize: Theme.scaled(13) }
                Text { 
                    text: BatteryService.health.toFixed(0) + "%" 
                    color: BatteryService.health > 80 ? Theme.powerGreen : (BatteryService.health > 50 ? Theme.powerYellow : Theme.powerRed)
                    font.bold: true
                    font.pixelSize: Theme.scaled(16) 
                }
            }

            Item { Layout.fillWidth: true }

            RowLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.scaled(8)
                Text { text: "Temp:"; color: Theme.subtext1; font.pixelSize: Theme.scaled(13) }
                Text { 
                    text: (BatteryService.temp > 0 ? BatteryService.temp.toFixed(1) : "35.0") + "°C"
                    color: BatteryService.temp > 45 ? Theme.powerRed : Theme.text
                    font.bold: true
                    font.pixelSize: Theme.scaled(16) 
                }
            }
        }
    }
}