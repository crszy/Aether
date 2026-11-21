import QtQuick
import qs.services

// Material 3 filled text field used across the Aether settings panel; commits on Enter or when it loses focus
Rectangle {
    id: root

    property alias text: input.text
    property string placeholder
    signal committed(string value)

    implicitWidth: 260
    implicitHeight: 38
    radius: 12
    color: Qt.alpha(AetherStyle.palette.m3surfaceContainerHighest, 0.9)
    clip: true

    TextInput {
        id: input

        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        verticalAlignment: Text.AlignVCenter
        color: AetherStyle.palette.m3onSurface
        selectionColor: AetherStyle.palette.m3primary
        selectedTextColor: AetherStyle.palette.m3onPrimary
        font.pointSize: 10
        selectByMouse: true
        onEditingFinished: root.committed(text)
    }

    Text {
        anchors.fill: input
        verticalAlignment: Text.AlignVCenter
        visible: !input.text && !input.activeFocus
        text: root.placeholder
        color: AetherStyle.palette.m3onSurfaceVariant
        font.pointSize: 10
    }

    // focus indicator
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: input.activeFocus ? 2 : 1
        color: input.activeFocus ? AetherStyle.palette.m3primary : AetherStyle.palette.m3outline
        opacity: input.activeFocus ? 1 : 0.6
    }
}
