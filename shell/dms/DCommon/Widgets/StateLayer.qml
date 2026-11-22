import QtQuick
import QtQuick.Templates as T
import qs.dms.DCommon.Common

MouseArea {
    id: root

    property bool disabled: false
    property bool hovered: control ? control.hovered : containsMouse
    // visualFocus is keyboard-only, so pointer focus never tints
    property bool focused: control ? control.visualFocus : false
    property T.AbstractButton control: null
    property color stateColor: Style.surfaceText
    property real cornerRadius: parent && parent.radius !== undefined ? parent.radius : Style.cornerRadius
    property real topLeftRadius: parent && parent.topLeftRadius !== undefined ? parent.topLeftRadius : cornerRadius
    property real topRightRadius: parent && parent.topRightRadius !== undefined ? parent.topRightRadius : cornerRadius
    property real bottomLeftRadius: parent && parent.bottomLeftRadius !== undefined ? parent.bottomLeftRadius : cornerRadius
    property real bottomRightRadius: parent && parent.bottomRightRadius !== undefined ? parent.bottomRightRadius : cornerRadius
    property var tooltipText: null
    property bool tooltipMultiline: false
    property string tooltipSide: "bottom"
    property bool enableRipple: Style.enableRippleEffects
    property int transitionDuration: Style.shorterDuration
    property var transitionCurve: Style.expressiveCurves.standardDecel

    readonly property bool isPressed: control ? control.down : pressed
    // the held ripple is the pressed layer whenever it can draw
    readonly property bool rippleDrawsPress: enableRipple && !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
    readonly property real stateOpacity: {
        if (disabled)
            return 0;
        if (isPressed && !rippleDrawsPress)
            return Style.stateLayerPressed;
        if (focused)
            return Style.stateLayerFocus;
        if (hovered)
            return Style.stateLayerHover;
        return 0;
    }
    readonly property bool controlPressed: control ? control.pressed : false

    anchors.fill: parent
    cursorShape: disabled ? Qt.ArrowCursor : Qt.PointingHandCursor
    hoverEnabled: true
    acceptedButtons: control ? Qt.NoButton : Qt.LeftButton

    readonly property bool controlFocused: control ? control.visualFocus : false

    function showTooltip() {
        presentTooltip(0);
    }

    function presentTooltip(delay) {
        if (!tooltipText)
            return;
        tooltipLoader.active = true;
        const host = tooltipLoader.item;
        host?.present(delay);
        if (!host?.shown)
            tooltipLoader.active = false;
    }

    onPressed: mouse => {
        if (!disabled && enableRipple)
            rippleLayer.trigger(mouse.x, mouse.y, true);
    }
    onReleased: rippleLayer.release()
    onCanceled: rippleLayer.release()

    onControlPressedChanged: {
        if (!controlPressed) {
            rippleLayer.release();
            return;
        }
        if (disabled || !enableRipple)
            return;
        const point = mapFromItem(control, control.pressX, control.pressY);
        rippleLayer.trigger(point.x, point.y, true);
    }

    Rectangle {
        id: stateRect

        property real stateAlpha: root.stateOpacity

        anchors.fill: parent
        radius: root.cornerRadius
        topLeftRadius: root.topLeftRadius
        topRightRadius: root.topRightRadius
        bottomLeftRadius: root.bottomLeftRadius
        bottomRightRadius: root.bottomRightRadius
        color: Style.withAlpha(root.stateColor, stateAlpha)

        Behavior on stateAlpha {
            enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
            DAnim {
                duration: root.transitionDuration
                easing.bezierCurve: root.transitionCurve
            }
        }
    }

    DRipple {
        id: rippleLayer
        anchors.fill: parent
        rippleColor: root.stateColor
        cornerRadius: root.cornerRadius
        topLeftRadius: root.topLeftRadius
        topRightRadius: root.topRightRadius
        bottomLeftRadius: root.bottomLeftRadius
        bottomRightRadius: root.bottomRightRadius
        enableRipple: root.enableRipple
    }

    onEntered: presentTooltip(Style.tooltipDelay)

    onExited: {
        if (!controlFocused)
            tooltipLoader.item?.dismiss();
    }

    onControlFocusedChanged: {
        if (controlFocused) {
            presentTooltip(0);
            return;
        }
        if (!containsMouse)
            tooltipLoader.item?.dismiss();
    }

    Loader {
        id: tooltipLoader
        active: false
        sourceComponent: DTooltipHost {
            text: root.tooltipText
            multiline: root.tooltipMultiline
            target: root
            side: root.tooltipSide
            enabled: !root.disabled

            readonly property bool idle: !shown && !presenting
            onIdleChanged: {
                if (!idle) {
                    releaseTimer.stop();
                    return;
                }
                releaseTimer.restart();
            }

            // idle turns true while the tooltip is still inside its own close() call, so wait before destroying it
            Timer {
                id: releaseTimer
                interval: Style.shorterDuration
                onTriggered: tooltipLoader.active = false
            }
        }
    }
}
