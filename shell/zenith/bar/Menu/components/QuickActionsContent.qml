import QtQuick
import QtQuick.Layouts
import Quickshell
import "../../../"
import "../../../services"

Rectangle {
    id: root
    Layout.fillWidth: true
    height: Theme.scaled(56)
    radius: Theme.bubbleRadiusMedium
    color: Theme.cardBackground
    border.color: Theme.glassBorder
    border.width: 1
    clip: true

    readonly property var tools: [
        {
            id: "snip",
            icon: "󰹑",
            label: "Snip",
            active: false,
            color: Theme.blue,
            action: function() { PowerToolsService.triggerScreenshot("region"); }
        },
        {
            id: "color",
            icon: "󰈊",
            label: "Color",
            active: false,
            color: Theme.lavender,
            action: function() { PowerToolsService.triggerColorPicker(); }
        },
        {
            id: "caffeine",
            icon: CaffeineService.active ? "󰅶" : "󰛊",
            label: "Caffeine",
            active: CaffeineService.active,
            color: Theme.powerYellow,
            action: function() { CaffeineService.toggle(); }
        },
        {
            id: "nightlight",
            icon: PowerToolsService.nightLight ? "󰖔" : "󰖙",
            label: "Night Light",
            active: PowerToolsService.nightLight,
            color: Theme.accentColor,
            action: function() { PowerToolsService.toggleNightLight(); }
        },
        {
            id: "dnd",
            icon: PowerToolsService.dnd ? "󰂛" : "󰂚",
            label: "DND",
            active: PowerToolsService.dnd,
            color: Theme.powerRed,
            action: function() { PowerToolsService.toggleDnd(); }
        },
        {
            id: "mic",
            icon: PowerToolsService.micMuted ? "󰍭" : "󰍬",
            label: PowerToolsService.micMuted ? "Muted" : "Mic",
            active: PowerToolsService.micMuted,
            color: Theme.powerRed,
            action: function() { PowerToolsService.toggleMicMute(); }
        }
    ]

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.scaled(6)
        spacing: Theme.scaled(8)

        Repeater {
            model: root.tools

            delegate: Rectangle {
                id: btn
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.bubbleRadiusSmall

                color: modelData.active
                    ? Qt.rgba(modelData.color.r, modelData.color.g, modelData.color.b, 0.20)
                    : (btnMouse.containsMouse ? Theme.cardBackgroundHover : "transparent")

                border.width: 0

                scale: btnMouse.pressed ? 0.94 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Theme.animEasing } }
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                RowLayout {
                    anchors.centerIn: parent
                    spacing: Theme.scaled(6)

                    Text {
                        text: modelData.icon
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(20)
                        color: modelData.active ? modelData.color : (btnMouse.containsMouse ? Theme.text : Theme.subtext0)
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Text {
                        text: modelData.label
                        font.pixelSize: Theme.scaled(10.5)
                        font.weight: modelData.active ? Font.Bold : Font.DemiBold
                        font.letterSpacing: 0.3
                        color: modelData.active ? modelData.color : (btnMouse.containsMouse ? Theme.text : Theme.subtext1)
                        visible: !Theme.isSmallScreen
                        Layout.alignment: Qt.AlignVCenter
                    }
                }

                MouseArea {
                    id: btnMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: modelData.action()
                }
            }
        }
    }
}
