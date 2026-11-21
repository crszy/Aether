import QtQuick
import QtQuick.Layouts
import qs.components
import qs.services

// number field with - / + used across the rice settings panel; whole numbers step by 1, decimals by 0.05
RowLayout {
    id: root

    property real value
    readonly property bool decimal: Math.abs(value % 1) > 0.0001 || (value > 0 && value < 1)
    readonly property real step: decimal ? 0.05 : 1
    signal committed(real value)

    function commit(v: real): void {
        const r = decimal ? Math.round(v * 100) / 100 : Math.round(v);
        root.value = r;
        root.committed(r);
    }

    spacing: 4

    GlassButton {
        icon: "remove"
        compact: true
        onClicked: root.commit(root.value - root.step)
    }

    Rectangle {
        implicitWidth: 72
        implicitHeight: 36
        radius: 12
        color: Qt.alpha(AetherStyle.palette.m3surfaceContainerHighest, 0.9)
        border.width: input.activeFocus ? 2 : 0
        border.color: AetherStyle.palette.m3primary

        TextInput {
            id: input

            anchors.fill: parent
            anchors.margins: 6
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: AetherStyle.palette.m3onSurface
            font.pointSize: 10
            selectByMouse: true
            text: String(root.value)
            validator: DoubleValidator {}
            onEditingFinished: {
                const v = parseFloat(text);
                if (!isNaN(v))
                    root.commit(v);
            }
        }
    }

    GlassButton {
        icon: "add"
        compact: true
        onClicked: root.commit(root.value + root.step)
    }
}
