import QtQuick
import Caelestia.Config
import qs.components
import qs.services

// Material 3 switch used across the Aether settings panel (the handle grows and shows a check when on)
Item {
    id: root

    property bool checked
    property color accent: AetherStyle.palette.m3primary
    signal toggled(bool value)

    implicitWidth: 52
    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? root.accent : Qt.alpha(AetherStyle.palette.m3surfaceContainerHighest, 0.95)
        border.width: root.checked ? 0 : 2
        border.color: AetherStyle.palette.m3outline

        Behavior on color {
            ColorAnimation {
                duration: 160
            }
        }
    }

    Rectangle {
        readonly property real knob: area.pressed ? 28 : root.checked ? 24 : 16

        width: knob
        height: knob
        radius: knob / 2
        anchors.verticalCenter: parent.verticalCenter
        x: root.checked ? root.width - width - 4 : (root.height - knob) / 2
        color: root.checked ? AetherStyle.palette.m3onPrimary : AetherStyle.palette.m3outline

        Behavior on x {
            NumberAnimation {
                duration: 200
                easing.type: Easing.OutBack
                easing.overshoot: 1.4
            }
        }
        Behavior on width {
            NumberAnimation {
                duration: 160
                easing.type: Easing.OutCubic
            }
        }
        Behavior on height {
            NumberAnimation {
                duration: 160
                easing.type: Easing.OutCubic
            }
        }

        MaterialIcon {
            anchors.centerIn: parent
            visible: root.checked
            text: "check"
            color: root.accent
            fontStyle: Tokens.font.icon.small
            scale: 0.75
        }
    }

    MouseArea {
        id: area

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.checked = !root.checked;
            root.toggled(root.checked);
        }
    }
}
