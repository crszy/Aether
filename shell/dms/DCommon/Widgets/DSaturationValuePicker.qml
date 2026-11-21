import QtQuick
import QtQuick.Controls as Controls
import Quickshell.Widgets
import qs.dms.DCommon.Common

Controls.Control {
    id: root

    property real hue: 0
    property real saturation: 1
    property real value: 1
    readonly property real radius: Math.min(Style.cornerRadiusL, width / 2, height / 2)
    readonly property real handleSize: Style.iconSizeMedium
    readonly property real handleInset: Math.max(handleSize / 2 + Style.outlineWidthFocused, radius - (radius - handleSize / 2 - Style.outlineWidth) / Math.SQRT2)

    signal colorChanged(real saturation, real value)

    implicitWidth: Style.fieldDefaultWidth
    implicitHeight: Style.fieldDefaultWidth
    focusPolicy: Qt.StrongFocus
    LayoutMirroring.enabled: false
    Accessible.role: Accessible.Slider
    Accessible.name: I18n.tr("Color")
    Accessible.description: I18n.tr("Saturation %1%, brightness %2%", "Current values in the two-dimensional color picker").arg(Math.round(saturation * 100)).arg(Math.round(value * 100))

    function commit(s, v) {
        if (!enabled)
            return;
        colorChanged(Math.max(0, Math.min(1, s)), Math.max(0, Math.min(1, v)));
    }

    function pick(x, y) {
        commit((x - handleInset) / Math.max(1, width - handleInset * 2), 1 - (y - handleInset) / Math.max(1, height - handleInset * 2));
    }

    Keys.onPressed: event => {
        const step = event.modifiers & Qt.ShiftModifier ? 0.1 : 0.01;
        switch (event.key) {
        case Qt.Key_Left:
            commit(saturation - step, value);
            break;
        case Qt.Key_Right:
            commit(saturation + step, value);
            break;
        case Qt.Key_Up:
            commit(saturation, value + step);
            break;
        case Qt.Key_Down:
            commit(saturation, value - step);
            break;
        case Qt.Key_Home:
            commit(0, value);
            break;
        case Qt.Key_End:
            commit(1, value);
            break;
        case Qt.Key_PageUp:
            commit(saturation, value + 0.1);
            break;
        case Qt.Key_PageDown:
            commit(saturation, value - 0.1);
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    Accessible.onIncreaseAction: commit(saturation + 0.01, value)
    Accessible.onDecreaseAction: commit(saturation - 0.01, value)

    background: ClippingRectangle {
        radius: root.radius
        color: root.enabled ? Qt.hsva(root.hue, 1, 1, 1) : Style.onSurface_12

        Rectangle {
            anchors.fill: parent
            visible: root.enabled
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop {
                    position: Math.min(0.5, root.handleInset / Math.max(1, root.width))
                    color: Style.contrastLight
                }
                GradientStop {
                    position: 1 - Math.min(0.5, root.handleInset / Math.max(1, root.width))
                    color: "transparent"
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: root.enabled
            gradient: Gradient {
                GradientStop {
                    position: Math.min(0.5, root.handleInset / Math.max(1, root.height))
                    color: "transparent"
                }
                GradientStop {
                    position: 1 - Math.min(0.5, root.handleInset / Math.max(1, root.height))
                    color: Style.contrastDark
                }
            }
        }
    }

    Rectangle {
        width: root.handleSize
        height: width
        x: root.handleInset + root.saturation * Math.max(0, root.width - root.handleInset * 2) - width / 2
        y: root.handleInset + (1 - root.value) * Math.max(0, root.height - root.handleInset * 2) - height / 2
        radius: width / 2
        color: root.enabled ? Qt.hsva(root.hue, root.saturation, root.value, 1) : Style.onSurface_38
        border.width: Style.outlineWidthFocused
        border.color: root.enabled ? Style.contrastLight : Style.onSurface_38

        Rectangle {
            anchors.fill: parent
            anchors.margins: -Style.outlineWidth
            radius: width / 2
            color: "transparent"
            border.width: Style.outlineWidth
            border.color: Style.contrastDark
            visible: root.enabled
        }
    }

    FocusRing {
        visible: root.visualFocus
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled
        cursorShape: Qt.CrossCursor
        preventStealing: true
        onPressed: mouse => {
            root.forceActiveFocus(Qt.MouseFocusReason);
            root.pick(mouse.x, mouse.y);
        }
        onPositionChanged: mouse => {
            if (pressed)
                root.pick(mouse.x, mouse.y);
        }
    }
}
