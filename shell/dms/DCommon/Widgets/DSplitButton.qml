pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common

Item {
    id: root

    property string text: ""
    property string iconName: ""
    property string size: "s"
    property string variant: "filled"
    property bool expanded: false
    property bool menuOnly: false
    property bool wrapText: true
    property real maximumWidth: Infinity
    property string tooltipText: ""
    property string menuTooltipText: text
    readonly property alias leadingButton: leading
    readonly property alias trailingButton: trailing

    signal clicked
    signal menuClicked

    readonly property var sizeTokens: ({
            xs: {
                height: Style.buttonHeightXS,
                leadingSpace: 12,
                trailingSpace: 10,
                iconSize: 20,
                menuIconSize: 22,
                menuSpace: 13,
                iconSpacing: 8,
                innerRadius: Style.cornerRadiusXS,
                activeInnerRadius: Style.cornerRadiusS,
                labelSize: 14,
                outlineWidth: 1
            },
            s: {
                height: Style.buttonHeightS,
                leadingSpace: 16,
                trailingSpace: 12,
                iconSize: 20,
                menuIconSize: 22,
                menuSpace: 13,
                iconSpacing: 8,
                innerRadius: Style.cornerRadiusXS,
                activeInnerRadius: Style.cornerRadiusM,
                labelSize: 14,
                outlineWidth: 1
            },
            m: {
                height: Style.buttonHeightM,
                leadingSpace: 24,
                trailingSpace: 24,
                iconSize: 24,
                menuIconSize: 26,
                menuSpace: 15,
                iconSpacing: 8,
                innerRadius: Style.cornerRadiusXS,
                activeInnerRadius: Style.cornerRadiusM,
                labelSize: 16,
                outlineWidth: 1
            },
            l: {
                height: 96,
                leadingSpace: 48,
                trailingSpace: 48,
                iconSize: 32,
                menuIconSize: 38,
                menuSpace: 29,
                iconSpacing: 12,
                innerRadius: Style.cornerRadiusS,
                activeInnerRadius: Style.cornerRadiusLIncreased,
                labelSize: 24,
                outlineWidth: 2
            },
            xl: {
                height: 136,
                leadingSpace: 64,
                trailingSpace: 64,
                iconSize: 40,
                menuIconSize: 50,
                menuSpace: 43,
                iconSpacing: 16,
                innerRadius: Style.cornerRadiusM,
                activeInnerRadius: Style.cornerRadiusLIncreased,
                labelSize: 32,
                outlineWidth: 3
            }
        })
    readonly property var metrics: sizeTokens[size] ?? sizeTokens.s
    readonly property real buttonHeight: metrics.height
    readonly property real labelSize: metrics.labelSize * Style.fontSizeMedium / 14
    readonly property real spacing: Style.spacingXXS
    readonly property real visualHeight: Math.max(buttonHeight, labelRow.implicitHeight)
    readonly property real trailingWidth: Math.max(Style.minimumTouchTargetSize, metrics.menuIconSize + metrics.menuSpace * 2)
    readonly property real leadingWidth: Math.max(Style.minimumTouchTargetSize, Math.ceil(leadingLabel.implicitWidth) + metrics.leadingSpace + metrics.trailingSpace + (iconName ? metrics.iconSize + metrics.iconSpacing : 0))
    readonly property color containerColor: {
        switch (variant) {
        case "tonal":
            return Style.secondaryContainer;
        case "outlined":
            return "transparent";
        case "elevated":
            return Style.surfaceContainerLow;
        default:
            return Style.primary;
        }
    }
    readonly property color contentColor: {
        switch (variant) {
        case "tonal":
            return Style.onSecondaryContainer;
        case "outlined":
            return Style.onSurfaceVariant;
        case "elevated":
            return Style.primary;
        default:
            return Style.onPrimary;
        }
    }

    implicitWidth: Math.min(maximumWidth, leadingWidth + spacing + trailingWidth)
    implicitHeight: Math.max(Style.minimumTouchTargetSize, visualHeight)
    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    component Segment: StyledButton {
        id: segment

        property bool isTrailing: false
        readonly property real outerRadius: Style.pillControls ? Style.fullRadius(width, root.visualHeight) : Style.cornerRadiusL
        property real innerRadius: {
            if (pressed && Style.shapeMorph)
                return Math.min(outerRadius, root.metrics.activeInnerRadius);
            if (isTrailing && root.expanded && Style.shapeMorph)
                return outerRadius;
            return Math.min(outerRadius, root.metrics.innerRadius);
        }
        readonly property bool outerOnLeft: isTrailing === I18n.isRtl
        readonly property color labelColor: enabled ? root.contentColor : Style.onSurface_38

        height: root.height
        topInset: (height - root.visualHeight) / 2
        bottomInset: topInset
        color: enabled ? root.containerColor : root.variant === "outlined" ? "transparent" : Style.onSurface_10
        radius: outerRadius
        topLeftRadius: outerOnLeft ? outerRadius : innerRadius
        bottomLeftRadius: topLeftRadius
        topRightRadius: outerOnLeft ? innerRadius : outerRadius
        bottomRightRadius: topRightRadius
        border.width: root.variant === "outlined" ? root.metrics.outlineWidth : 0
        border.color: enabled ? Style.outlineVariant : Style.onSurface_12

        Behavior on innerRadius {
            enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
            DAnim {
                duration: Style.expressiveDurations.expressiveEffects
                easing.bezierCurve: Style.expressiveCurves.standard
            }
        }

        Loader {
            anchors.fill: parent
            anchors.topMargin: segment.topInset
            anchors.bottomMargin: segment.bottomInset
            z: -1
            active: root.variant === "elevated" && segment.enabled && Style.elevationEnabled
            sourceComponent: ElevationShadow {
                level: segment.hovered ? Style.elevationLevel2 : Style.elevationLevel1
                targetColor: segment.color
                topLeftRadius: segment.topLeftRadius
                topRightRadius: segment.topRightRadius
                bottomLeftRadius: segment.bottomLeftRadius
                bottomRightRadius: segment.bottomRightRadius
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: segment.topInset
            anchors.bottomMargin: segment.bottomInset
            visible: segment.isTrailing && root.expanded && segment.enabled
            topLeftRadius: segment.topLeftRadius
            topRightRadius: segment.topRightRadius
            bottomLeftRadius: segment.bottomLeftRadius
            bottomRightRadius: segment.bottomRightRadius
            color: Style.withAlpha(root.contentColor, Style.stateLayerPressed)
        }

        StateLayer {
            anchors.topMargin: segment.topInset
            anchors.bottomMargin: segment.bottomInset
            control: segment
            disabled: !segment.enabled
            stateColor: root.contentColor
            tooltipText: segment.isTrailing ? root.menuTooltipText : root.tooltipText
        }

        FocusRing {
            anchors.topMargin: segment.topInset - Style.focusRingOffset
            anchors.bottomMargin: segment.bottomInset - Style.focusRingOffset
            topLeftRadius: segment.topLeftRadius + Style.focusRingOffset
            topRightRadius: segment.topRightRadius + Style.focusRingOffset
            bottomLeftRadius: segment.bottomLeftRadius + Style.focusRingOffset
            bottomRightRadius: segment.bottomRightRadius + Style.focusRingOffset
            visible: segment.visualFocus
        }
    }

    Segment {
        id: leading
        x: I18n.isRtl ? root.trailingWidth + root.spacing : 0
        width: Math.max(0, root.width - root.trailingWidth - root.spacing)
        Accessible.name: root.text
        Accessible.description: root.Accessible.description
        onClicked: {
            if (root.menuOnly) {
                root.menuClicked();
                return;
            }
            root.clicked();
        }
        Keys.onDownPressed: event => {
            root.menuClicked();
            event.accepted = true;
        }

        Row {
            id: labelRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: root.metrics.leadingSpace
            anchors.rightMargin: root.metrics.trailingSpace
            anchors.verticalCenter: parent.verticalCenter
            spacing: root.metrics.iconSpacing

            DIcon {
                name: root.iconName
                size: root.metrics.iconSize
                color: leading.labelColor
                visible: root.iconName !== ""
                anchors.verticalCenter: parent.verticalCenter
            }

            StyledText {
                id: leadingLabel
                width: Math.max(0, labelRow.width - (root.iconName ? root.metrics.iconSize + labelRow.spacing : 0))
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                color: leading.labelColor
                font.family: Style.fontFamily
                font.pixelSize: root.labelSize
                font.weight: root.size === "l" || root.size === "xl" ? Style.fontWeight : Style.fontWeightMedium
                wrapMode: root.wrapText ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
                elide: root.wrapText ? Text.ElideNone : Text.ElideRight
            }
        }
    }

    Segment {
        id: trailing
        isTrailing: true
        x: I18n.isRtl ? 0 : root.width - width
        width: root.trailingWidth
        Accessible.name: root.menuTooltipText
        Accessible.checkable: true
        Accessible.checked: root.expanded
        onClicked: root.menuClicked()
        Keys.onDownPressed: event => {
            root.menuClicked();
            event.accepted = true;
        }

        DIcon {
            anchors.centerIn: parent
            name: "keyboard_arrow_down"
            size: root.metrics.menuIconSize
            color: trailing.labelColor
            rotation: root.expanded ? 180 : 0

            Behavior on rotation {
                enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                DAnim {
                    duration: Style.expressiveDurations.expressiveEffects
                    easing.bezierCurve: Style.expressiveCurves.standard
                }
            }
        }
    }
}
