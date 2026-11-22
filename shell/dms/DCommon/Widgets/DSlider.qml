import QtQuick
import QtQuick.Controls as Controls
import Quickshell.Widgets
import qs.dms.DCommon.Common

Controls.Control {
    id: slider

    readonly property bool containerScrolls: {
        for (let ancestor = parent; ancestor; ancestor = ancestor.parent) {
            if ("flickableDirection" in ancestor && ancestor.contentHeight > ancestor.height)
                return true;
        }
        return false;
    }

    focusPolicy: enabled ? wheelEnabled ? Qt.WheelFocus : Qt.StrongFocus : Qt.NoFocus
    wheelEnabled: true

    Binding on wheelEnabled {
        when: slider.containerScrolls
        value: false
        restoreMode: Binding.RestoreBindingOrValue
    }

    property int value: 50
    property int minimum: 0
    property int maximum: 100
    property int step: 1
    property int wheelStep: keyStep
    property bool upDownKeysStep: true
    property string startIcon: ""
    property string endIcon: ""
    property alias leftIcon: slider.startIcon // ! TODO deprecate me after 1.7 release
    property alias rightIcon: slider.endIcon // ! TODO deprecate me after 1.7 release
    property bool iconsClickable: false
    property string insetIcon: ""
    property real insetIconRotation: 0
    property string insetIconPosition: "start"
    property bool insetIconClickable: false
    property string insetIconTooltip: ""
    property string insetIconLabel: ""
    property string unit: ""
    property bool showValue: true
    property bool showStops: false
    property bool isDragging: false
    property bool centerMinimum: false
    property real valueOverride: -1
    property int decimals: 0
    property bool alwaysShowValue: false
    property string size: "s"
    readonly property real handleWidth: Style.sliderHandleWidth
    readonly property real pressedHandleWidth: Style.sliderHandleWidthPressed
    readonly property bool containsMouse: sliderMouseArea.containsMouse
    readonly property var focusTargets: [startIconLoader.action, insetAction.enabled ? insetAction : null, slider, endIconLoader.action].filter(item => item && item.enabled)

    // Unused; third-party plugins still assign it and fail to load without it.
    property color thumbOutlineColor
    property color fillColor: Style.primary
    property color fillTextColor: Style.onPrimary
    property color trackColor: Style.secondaryContainer
    property Gradient trackGradient: null
    property color trackTextColor: Style.onSecondaryContainer
    property color focusRingColor: Style.focusRingColor
    property bool usePopupTransparency: !Style.isFloatingWindow(slider)
    readonly property real trackAlpha: Math.max(Style.sliderTrackMinAlpha, usePopupTransparency ? Style.foregroundAlpha : Style.floatingWindowForegroundAlpha)

    signal insetIconClicked
    signal sliderValueChanged(int newValue)
    signal sliderDragFinished(int finalValue)

    onIsDraggingChanged: insetAction.syncTooltip()

    function formatValue(v) {
        if (decimals <= 0)
            return Math.round(v) + unit;
        return (v / Math.pow(10, decimals)).toFixed(decimals) + unit;
    }

    function ratioForValue(v) {
        const range = maximum - minimum;
        const raw = range === 0 ? 0 : (v - minimum) / range;
        const clamped = Math.max(0, Math.min(1, raw));
        return centerMinimum ? (0.5 + clamped * 0.5) : clamped;
    }

    readonly property real ratio: ratioForValue(value)
    LayoutMirroring.enabled: I18n.isRtl
    property real insetIconSize: {
        switch (size) {
        case "s":
            return Style.iconSizeSmall;
        case "xl":
            return Style.iconSizeLarge;
        default:
            return Style.iconSize;
        }
    }
    property real trackHeight: {
        switch (size) {
        case "s":
            return Style.sliderTrackHeightS;
        case "m":
            return Style.sliderTrackHeightM;
        case "l":
            return Style.sliderTrackHeightL;
        case "xl":
            return Style.sliderTrackHeightXL;
        default:
            return Style.sliderTrackHeight;
        }
    }
    property real handleHeight: {
        switch (size) {
        case "s":
            return Style.sliderHandleHeightS;
        case "m":
            return Style.sliderHandleHeightM;
        case "l":
            return Style.sliderHandleHeightL;
        case "xl":
            return Style.sliderHandleHeightXL;
        default:
            return Style.sliderHandleHeight;
        }
    }
    property real trackCornerRadius: {
        switch (size) {
        case "s":
            return Style.sliderTrackCornerRadiusS;
        case "m":
            return Style.sliderTrackCornerRadiusM;
        case "l":
            return Style.sliderTrackCornerRadiusL;
        case "xl":
            return Style.sliderTrackCornerRadiusXL;
        default:
            return Style.sliderTrackCornerRadius;
        }
    }
    readonly property real outsideCorner: Style.scaledRadius(trackCornerRadius, trackHeight / 2)
    // Fixed corner mode would round the inner ends to the full radius and split the track into two pills.
    readonly property real insideCorner: Math.min(Style.sliderTrackInsideCornerRadius * Style.shapeScale, trackHeight / 2)
    readonly property real visualRatio: mirrored ? 1 - ratio : ratio
    readonly property int tickCount: {
        if (step <= 1)
            return 0;
        const steps = Math.ceil((maximum - minimum) / step);
        return steps >= 2 ? steps + 1 : 0;
    }
    readonly property int keyStep: step > 1 ? step : Math.max(1, Math.round((maximum - minimum) / 100))
    readonly property int pageSteps: Math.max(1, Math.min(10, Math.round((maximum - minimum) / keyStep / 10)))

    height: handleHeight + Style.spacingXS
    readonly property int minimumValue: minimum
    readonly property int maximumValue: maximum
    readonly property int stepSize: keyStep
    Accessible.role: Accessible.Slider
    Accessible.focusable: enabled
    Accessible.onIncreaseAction: {
        if (enabled)
            stepBy(1);
    }
    Accessible.onDecreaseAction: {
        if (enabled)
            stepBy(-1);
    }

    function commit(newValue) {
        const clamped = Math.max(minimum, Math.min(maximum, newValue));
        if (clamped === value)
            return;
        value = clamped;
        sliderValueChanged(clamped);
    }

    function stepBy(direction, amount) {
        let next = value + direction * (amount ?? keyStep);
        if (step > 1)
            next = minimum + Math.round((next - minimum) / step) * step;
        commit(Math.round(next));
        sliderDragFinished(value);
    }

    function updateValueFromPosition(x) {
        if (sliderTrack.width <= handleWidth)
            return;
        let ratio = Math.max(0, Math.min(1, (x - handleWidth / 2) / (sliderTrack.width - handleWidth)));
        if (mirrored)
            ratio = 1 - ratio;
        if (centerMinimum)
            ratio = Math.max(0, (ratio - 0.5) * 2);
        let rawValue = minimum + ratio * (maximum - minimum);
        let newValue = step > 1 ? minimum + Math.round((rawValue - minimum) / step) * step : Math.round(rawValue);
        commit(newValue);
    }

    Keys.onPressed: event => {
        if (!enabled)
            return;
        const upKey = mirrored ? Qt.Key_Left : Qt.Key_Right;
        const downKey = mirrored ? Qt.Key_Right : Qt.Key_Left;
        if (!upDownKeysStep && (event.key === Qt.Key_Up || event.key === Qt.Key_Down))
            return;
        switch (event.key) {
        case upKey:
        case Qt.Key_Up:
            stepBy(1);
            event.accepted = true;
            break;
        case downKey:
        case Qt.Key_Down:
            stepBy(-1);
            event.accepted = true;
            break;
        case Qt.Key_PageUp:
            stepBy(pageSteps);
            event.accepted = true;
            break;
        case Qt.Key_PageDown:
            stepBy(-pageSteps);
            event.accepted = true;
            break;
        case Qt.Key_Home:
            commit(minimum);
            sliderDragFinished(value);
            event.accepted = true;
            break;
        case Qt.Key_End:
            commit(maximum);
            sliderDragFinished(value);
            event.accepted = true;
            break;
        }
    }

    component GradientTrack: ClippingRectangle {
        id: gradientTrack

        required property Item track

        anchors.fill: parent
        color: "transparent"
        topLeftRadius: track.topLeftRadius
        topRightRadius: track.topRightRadius
        bottomLeftRadius: track.bottomLeftRadius
        bottomRightRadius: track.bottomRightRadius

        Rectangle {
            id: gradientSurface

            x: -gradientTrack.track.x
            width: sliderTrack.width
            height: gradientTrack.height
            gradient: slider.trackGradient
            transform: Scale {
                origin.x: gradientSurface.width / 2
                xScale: slider.mirrored ? -1 : 1
            }
        }
    }

    component SideIcon: Loader {
        id: iconLoader

        required property string iconName
        required property int direction
        readonly property Item action: slider.iconsClickable ? item : null

        width: slider.iconsClickable ? Style.iconButtonSize : Style.iconSize
        height: width
        anchors.verticalCenter: parent.verticalCenter
        visible: iconName.length > 0
        active: visible
        sourceComponent: slider.iconsClickable ? actionComponent : iconComponent

        Component {
            id: iconComponent

            DIcon {
                name: iconLoader.iconName
                size: Style.iconSize
                color: slider.enabled ? Style.surfaceText : Style.onSurface_38
            }
        }

        Component {
            id: actionComponent

            DActionButton {
                iconName: iconLoader.iconName
                iconSize: Style.iconSize
                iconColor: Style.surfaceText
                enabled: slider.enabled && (iconLoader.direction < 0 ? slider.value > slider.minimum : slider.value < slider.maximum)
                Accessible.name: iconLoader.direction < 0 ? I18n.tr("Decrease", "Accessible name for a button that decreases a numeric value") : I18n.tr("Increase", "Accessible name for a button that increases a numeric value")
                onClicked: slider.stepBy(iconLoader.direction)
            }
        }
    }

    contentItem: Row {
        anchors.centerIn: parent
        width: parent.width
        spacing: Style.spacingM
        LayoutMirroring.enabled: slider.mirrored

        SideIcon {
            id: startIconLoader
            iconName: slider.startIcon
            direction: -1
        }

        Item {
            id: sliderTrack

            readonly property real travel: Math.max(0, width - slider.handleWidth)
            readonly property real handleLeft: slider.handleWidth / 2 + travel * slider.visualRatio - sliderHandle.width / 2
            readonly property real gap: Style.sliderHandleGap
            readonly property real filledStart: slider.mirrored ? sliderHandle.x + sliderHandle.width + gap : 0
            readonly property real filledEnd: slider.mirrored ? width : sliderHandle.x - gap
            readonly property real emptyStart: slider.mirrored ? 0 : sliderHandle.x + sliderHandle.width + gap
            readonly property real emptyEnd: slider.mirrored ? sliderHandle.x - gap : width
            readonly property real tickSpacing: slider.tickCount > 1 ? travel / (slider.tickCount - 1) : 0
            readonly property bool ticksVisible: slider.showStops && slider.tickCount > 0 && tickSpacing >= Style.sliderTickSize + Style.sliderHandleGap
            readonly property bool insetIconVisible: slider.insetIcon.length > 0 && ["s", "m", "l", "xl"].indexOf(slider.size) !== -1 && !slider.centerMinimum
            readonly property bool insetIconLeftAligned: (!slider.mirrored && slider.insetIconPosition === "start") || (slider.mirrored && slider.insetIconPosition === "end")
            readonly property real insetIconExtent: Style.spacingXS + movingInsetIcon.width
            readonly property bool insetIconBehindHandle: {
                if (insetIconLeftAligned)
                    return sliderHandle.x - gap < insetIconExtent;
                return width - sliderHandle.x - sliderHandle.width - gap < insetIconExtent;
            }

            width: parent.width - (slider.startIcon.length > 0 ? startIconLoader.width + Style.spacingM : 0) - (slider.endIcon.length > 0 ? endIconLoader.width + Style.spacingM : 0)
            height: slider.handleHeight
            anchors.verticalCenter: parent.verticalCenter

            StyledRect {
                id: activeTrack
                x: sliderTrack.filledStart
                width: Math.max(0, sliderTrack.filledEnd - sliderTrack.filledStart)
                height: slider.trackHeight
                anchors.verticalCenter: parent.verticalCenter
                topLeftRadius: slider.mirrored ? slider.insideCorner : slider.outsideCorner
                bottomLeftRadius: topLeftRadius
                topRightRadius: slider.mirrored ? slider.outsideCorner : slider.insideCorner
                bottomRightRadius: topRightRadius
                color: !slider.enabled ? Style.onSurface_38 : slider.trackGradient ? "transparent" : slider.fillColor
                visible: width > 0

                Loader {
                    anchors.fill: parent
                    active: slider.enabled && slider.trackGradient !== null
                    sourceComponent: GradientTrack {
                        track: activeTrack
                    }
                }
            }

            StyledRect {
                id: inactiveTrack
                x: sliderTrack.emptyStart
                width: Math.max(0, sliderTrack.emptyEnd - sliderTrack.emptyStart)
                height: slider.trackHeight
                anchors.verticalCenter: parent.verticalCenter
                topLeftRadius: slider.mirrored ? slider.outsideCorner : slider.insideCorner
                bottomLeftRadius: topLeftRadius
                topRightRadius: slider.mirrored ? slider.insideCorner : slider.outsideCorner
                bottomRightRadius: topRightRadius
                color: !slider.enabled ? Style.onSurface_12 : slider.trackGradient ? "transparent" : Style.blendAlpha(slider.trackColor, slider.trackAlpha)
                visible: width > 0

                Loader {
                    anchors.fill: parent
                    active: slider.enabled && slider.trackGradient !== null
                    sourceComponent: GradientTrack {
                        track: inactiveTrack
                    }
                }

                StyledRect {
                    width: Style.sliderStopSize
                    height: Style.sliderStopSize
                    radius: Style.fullRadius(width, height)
                    x: slider.mirrored ? Style.sliderHandleGap : parent.width - Style.sliderHandleGap - width
                    anchors.verticalCenter: parent.verticalCenter
                    color: slider.enabled ? slider.fillColor : Style.onSurface_38
                    visible: slider.showStops && !(sliderTrack.insetIconVisible && slider.insetIconPosition === "end") && parent.width > Style.sliderHandleGap * 2 + width
                }
            }

            Repeater {
                model: sliderTrack.ticksVisible ? slider.tickCount : 0

                StyledRect {
                    required property int index
                    readonly property real tickRatio: slider.ratioForValue(slider.minimum + index * slider.step)
                    readonly property real tickX: slider.handleWidth / 2 + sliderTrack.travel * (slider.mirrored ? 1 - tickRatio : tickRatio)
                    readonly property bool onFilled: slider.mirrored ? tickX > sliderHandle.x + sliderHandle.width : tickX < sliderHandle.x
                    width: Style.sliderTickSize
                    height: width
                    radius: Style.fullRadius(width, height)
                    x: tickX - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: onFilled ? slider.fillTextColor : Style.onSurfaceVariant
                    visible: index !== 0 && index !== slider.tickCount - 1 && Math.abs(tickX - sliderHandle.x - sliderHandle.width / 2) > Style.sliderHandleGap * 2
                }
            }

            StyledRect {
                id: sliderHandle

                width: sliderMouseArea.pressed ? slider.pressedHandleWidth : slider.handleWidth
                height: slider.handleHeight
                radius: Style.fullRadius(width, height)
                x: sliderTrack.handleLeft
                anchors.verticalCenter: parent.verticalCenter
                color: slider.enabled ? slider.fillColor : Style.onSurface_38
                border.width: 0

                Behavior on width {
                    enabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                    DAnim {
                        duration: Style.expressiveDurations.expressiveEffects
                        easing.bezierCurve: Style.expressiveCurves.standard
                    }
                }

                FocusRing {
                    visible: slider.visualFocus
                    border.color: slider.focusRingColor
                }
            }

            DIcon {
                id: movingInsetIcon
                name: slider.insetIcon
                rotation: slider.insetIconRotation
                size: slider.insetIconSize
                color: slider.enabled ? (slider.insetIconPosition === "start" ? slider.trackTextColor : slider.fillTextColor) : Style.onSurface_38
                anchors.verticalCenter: parent.verticalCenter
                x: sliderTrack.insetIconLeftAligned ? sliderHandle.x + sliderHandle.width + Style.spacingS : sliderHandle.x - width - Style.spacingS
                opacity: sliderTrack.insetIconBehindHandle ? 1 : 0
                visible: sliderTrack.insetIconVisible

                Behavior on opacity {
                    enabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                    DAnim {
                        duration: Style.shorterDuration
                        easing.bezierCurve: Style.expressiveCurves.standard
                    }
                }
            }

            DIcon {
                name: slider.insetIcon
                rotation: slider.insetIconRotation
                size: slider.insetIconSize
                color: slider.enabled ? (slider.insetIconPosition === "start" ? slider.fillTextColor : slider.trackTextColor) : Style.onSurface_38
                anchors.verticalCenter: parent.verticalCenter
                x: sliderTrack.insetIconLeftAligned ? Style.spacingXS : sliderTrack.width - width - Style.spacingXS
                opacity: sliderTrack.insetIconBehindHandle ? 0 : 1
                visible: sliderTrack.insetIconVisible

                Behavior on opacity {
                    enabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                    DAnim {
                        duration: Style.shorterDuration
                        easing.bezierCurve: Style.expressiveCurves.standard
                    }
                }
            }

            StyledButton {
                id: insetAction

                readonly property bool containsPointer: sliderMouseArea.containsMouse && containsPosition(sliderMouseArea.mouseX, sliderMouseArea.mouseY)
                readonly property real iconX: sliderTrack.insetIconBehindHandle ? movingInsetIcon.x : (sliderTrack.insetIconLeftAligned ? Style.spacingXS : sliderTrack.width - movingInsetIcon.width - Style.spacingXS)
                x: Math.max(0, Math.min(sliderTrack.width - width, iconX + (movingInsetIcon.width - width) / 2))
                width: Math.min(sliderTrack.width, Style.iconButtonSize)
                height: slider.trackHeight
                anchors.verticalCenter: parent.verticalCenter
                visible: sliderTrack.insetIconVisible && slider.insetIconClickable
                enabled: slider.enabled && visible
                Accessible.name: slider.insetIconLabel || slider.insetIconTooltip
                onClicked: activate()

                function activate() {
                    if (enabled)
                        slider.insetIconClicked();
                }

                function containsPosition(px, py) {
                    if (!enabled || px < x || px > x + width || py < y || py > y + height)
                        return false;
                    return px < sliderHandle.x - sliderTrack.gap || px > sliderHandle.x + sliderHandle.width + sliderTrack.gap;
                }

                function syncTooltip() {
                    if (!containsPointer || slider.isDragging) {
                        actionTooltip.dismiss();
                        return;
                    }
                    actionTooltip.schedule();
                }

                onContainsPointerChanged: syncTooltip()

                FocusRing {
                    visible: insetAction.visualFocus
                    radius: Style.fullRadius(width, height)
                    border.color: slider.focusRingColor
                }

                DTooltipHost {
                    id: actionTooltip
                    text: slider.insetIconTooltip
                    target: insetAction
                    side: "top"
                }
            }

            MouseArea {
                id: sliderMouseArea

                property bool pressedInsetIcon: false
                property real pressX: 0
                property real pressY: 0

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                enabled: slider.enabled
                preventStealing: true
                acceptedButtons: Qt.LeftButton
                onWheel: wheelEvent => {
                    if (!slider.wheelEnabled || wheelEvent.angleDelta.y === 0) {
                        wheelEvent.accepted = false;
                        return;
                    }
                    slider.stepBy(wheelEvent.angleDelta.y > 0 ? 1 : -1, slider.wheelStep);
                    wheelEvent.accepted = true;
                }
                onPressed: mouse => {
                    pressX = mouse.x;
                    pressY = mouse.y;
                    pressedInsetIcon = insetAction.containsPosition(mouse.x, mouse.y);
                    if (pressedInsetIcon)
                        return;
                    slider.forceActiveFocus(Qt.MouseFocusReason);
                    slider.isDragging = true;
                    updateValueFromPosition(mouse.x);
                }
                onReleased: {
                    if (pressedInsetIcon && !slider.isDragging)
                        insetAction.activate();
                    if (slider.isDragging)
                        slider.sliderDragFinished(slider.value);
                    slider.isDragging = false;
                    pressedInsetIcon = false;
                }
                onCanceled: {
                    slider.isDragging = false;
                    pressedInsetIcon = false;
                }
                onPositionChanged: mouse => {
                    if (!pressed || !slider.enabled)
                        return;
                    if (pressedInsetIcon && !slider.isDragging && Math.hypot(mouse.x - pressX, mouse.y - pressY) < Qt.styleHints.startDragDistance)
                        return;
                    slider.forceActiveFocus(Qt.MouseFocusReason);
                    slider.isDragging = true;
                    updateValueFromPosition(mouse.x);
                }
            }

            Controls.ToolTip {
                id: valueTooltip

                width: tooltipText.reservedWidth + Style.spacingM * 2
                height: tooltipText.contentHeight + Style.spacingS * 2
                padding: 0
                horizontalPadding: 0
                margins: Style.spacingXS
                x: Math.max(0, Math.min(sliderTrack.width - width, sliderHandle.x + sliderHandle.width / 2 - width / 2))
                y: -height - Style.spacingXS
                visible: slider.visible && slider.enabled && slider.showValue && (slider.alwaysShowValue || (sliderMouseArea.containsMouse && !insetAction.containsPointer) || slider.isDragging)
                closePolicy: Controls.Popup.NoAutoClose
                modal: false
                dim: false
                focus: false

                Binding {
                    target: valueTooltip.contentItem?.parent ?? null
                    property: "containmentMask"
                    value: QtObject {
                        function contains(position: point): bool {
                            return false;
                        }
                    }
                }

                background: StyledRect {
                    radius: Style.fullRadius(width, height)
                    color: slider.fillColor
                }

                contentItem: NumericText {
                    id: tooltipText

                    text: slider.formatValue(slider.valueOverride >= 0 ? slider.valueOverride : slider.value)
                    reserveText: {
                        let widest = "";
                        const samples = [slider.minimum, slider.maximum];
                        if (slider.valueOverride >= 0)
                            samples.push(slider.valueOverride);
                        for (let i = 0; i < samples.length; i++) {
                            const candidate = slider.formatValue(samples[i]);
                            if (candidate.length > widest.length)
                                widest = candidate;
                        }
                        return widest;
                    }
                    font.pixelSize: Style.fontSizeSmall
                    color: slider.fillTextColor
                    font.weight: Style.fontWeightMedium
                    verticalAlignment: Text.AlignVCenter
                    font.hintingPreference: Font.PreferFullHinting
                }

                enter: Transition {
                    enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                    DAnim {
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: Style.expressiveDurations.expressiveEffects
                        easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                    }
                    DAnim {
                        property: "scale"
                        from: Style.popupEnterScale
                        to: 1
                        duration: Style.expressiveDurations.expressiveFastSpatial
                        easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
                    }
                }

                exit: Transition {
                    enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                    DAnim {
                        property: "opacity"
                        from: 1
                        to: 0
                        duration: Style.expressiveDurations.expressiveEffects
                        easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                    }
                }
            }
        }

        SideIcon {
            id: endIconLoader
            iconName: slider.endIcon
            direction: 1
        }
    }
}
