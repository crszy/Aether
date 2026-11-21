// Discord-style title bar for an app: Discord's flat, very dark strip with the title in the middle and its thin
// minimise / square / X glyphs at the right, a soft grey hover and a red close hover. Discord's dark theme colours,
// drawn here (no Discord artwork).
import QtQuick
import qs.modules.captions

Rectangle {
    id: root

    required property var skin

    readonly property CaptionTone tone: CaptionTone {
        skin: root.skin
        own: "#121214"
    }
    // Discord's dark theme colours, or its light theme's on a light app: text, interactive glyphs, hover, danger
    readonly property color text: tone.light ? "#313338" : "#dbdee1"
    readonly property color textMuted: tone.light ? "#5c5e66" : "#949ba4"
    readonly property color glyph: tone.light ? "#4e5058" : "#b5bac1"
    readonly property color glyphHover: tone.light ? "#060607" : "#dbdee1"
    readonly property color hover: tone.light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.08)
    readonly property color danger: "#da373c"
    readonly property real buttonWidth: Math.round(height * 1.2)

    color: tone.background
    topLeftRadius: skin.cornerRadius
    topRightRadius: skin.cornerRadius

    CaptionArea {
        skin: root.skin
    }

    CaptionIcon {
        skin: root.skin
        x: title.x + (title.width - title.contentWidth) / 2 - width - 6
        anchors.verticalCenter: parent.verticalCenter
        size: Math.min(16, root.height - 10)
    }

    Text {
        id: title

        readonly property real reserve: buttons.width + 8

        x: reserve
        width: Math.max(0, root.width - 2 * reserve)
        anchors.verticalCenter: parent.verticalCenter
        text: root.skin.title
        color: root.skin.active ? root.text : root.textMuted
        font.pixelSize: Math.round(Math.min(14, root.height * 0.45))
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }

    component Button: CaptionButton {
        id: button

        property bool danger: false
        readonly property color glyphColor: hovered ? (danger ? "white" : root.glyphHover) : root.glyph

        width: root.buttonWidth
        height: root.height

        Rectangle {
            anchors.fill: parent
            color: button.danger ? root.danger : root.hover
            opacity: button.hovered ? (button.down && !button.danger ? 0.7 : 1) : 0
            topRightRadius: button.danger ? root.skin.cornerRadius : 0

            Behavior on opacity {
                NumberAnimation {
                    duration: 90
                }
            }
        }
    }

    Row {
        id: buttons

        anchors.right: parent.right
        height: parent.height

        Button {
            id: minButton

            visible: root.skin.canMinimize
            onClicked: root.skin.minimize()

            Rectangle {
                anchors.centerIn: parent
                width: 10
                height: 1
                color: minButton.glyphColor
            }
        }

        Button {
            id: maxButton

            visible: root.skin.canMaximize
            onClicked: root.skin.toggleMaximize()

            Rectangle {
                anchors.centerIn: parent
                width: 10
                height: 10
                color: "transparent"
                border.width: 1
                border.color: maxButton.glyphColor
                radius: 1
            }
        }

        Button {
            id: closeButton

            danger: true
            onClicked: root.skin.close()

            Repeater {
                model: [45, -45]

                Rectangle {
                    required property int modelData

                    anchors.centerIn: parent
                    width: 13
                    height: 1
                    antialiasing: true
                    rotation: modelData
                    color: closeButton.glyphColor
                }
            }
        }
    }
}
