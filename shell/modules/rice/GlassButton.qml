import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

// Material 3 button used across the Aether settings panel: tonal by default, filled when highlighted,
// a round icon button when compact. Colours come from your scheme.
Rectangle {
    id: root

    property string text
    property string icon
    property bool compact: false
    property bool highlighted: false
    property color accent: AetherStyle.palette.m3primary
    signal clicked

    readonly property color fg: highlighted ? AetherStyle.palette.m3onPrimary : AetherStyle.palette.m3onSecondaryContainer

    implicitWidth: compact ? 36 : row.implicitWidth + 28
    implicitHeight: compact ? 36 : 38
    radius: height / 2
    color: highlighted ? root.accent : Qt.alpha(AetherStyle.palette.m3secondaryContainer, 0.9)
    scale: area.pressed ? 0.95 : 1

    Behavior on scale {
        NumberAnimation {
            duration: 120
            easing.type: Easing.OutCubic
        }
    }

    // state layer
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: root.fg
        opacity: area.pressed ? 0.16 : area.containsMouse ? 0.08 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: 120
            }
        }
    }

    RowLayout {
        id: row

        anchors.centerIn: parent
        spacing: 8

        MaterialIcon {
            visible: root.icon.length > 0
            text: root.icon
            color: root.fg
            fontStyle: Tokens.font.icon.small
        }
        StyledText {
            visible: root.text.length > 0
            text: root.text
            color: root.fg
            font: Tokens.font.label.large
        }
    }

    MouseArea {
        id: area

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
