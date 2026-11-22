pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common

StyledButton {
    id: root

    property string iconName: ""
    property string colorRole: "primaryContainer"
    property bool busy: false
    property var tooltipText: null
    property string tooltipSide: "top"

    readonly property bool extended: text !== ""
    readonly property color containerColor: {
        switch (colorRole) {
        case "secondaryContainer":
            return Style.secondaryContainer;
        case "tertiaryContainer":
            return Style.tertiaryContainer;
        case "primary":
            return Style.primary;
        default:
            return Style.primaryContainer;
        }
    }
    readonly property color contentColor: {
        switch (colorRole) {
        case "secondaryContainer":
            return Style.onSecondaryContainer;
        case "tertiaryContainer":
            return Style.onTertiaryContainer;
        case "primary":
            return Style.onPrimary;
        default:
            return Style.onPrimaryContainer;
        }
    }

    implicitHeight: Style.buttonHeightM
    implicitWidth: extended ? Math.max(implicitHeight, contentRow.implicitWidth + Style.spacingL * 2) : implicitHeight
    radius: Math.min(Style.cornerRadiusL, height / 2)
    color: containerColor
    Accessible.name: text || tooltipText || iconName

    Loader {
        anchors.fill: parent
        z: -1
        active: Style.elevationEnabled
        sourceComponent: ElevationShadow {
            level: Style.elevationLevel3
            targetColor: root.color
            targetRadius: root.radius
        }
    }

    StateLayer {
        control: root
        disabled: !root.enabled
        stateColor: root.contentColor
        transitionDuration: Style.expressiveDurations.expressiveEffects
        transitionCurve: Style.expressiveCurves.expressiveEffects
        tooltipText: root.tooltipText
        tooltipSide: root.tooltipSide
    }

    FocusRing {
        visible: root.visualFocus
    }

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: Style.spacingS

        Item {
            width: Style.iconSize
            height: Style.iconSize
            visible: root.busy || root.iconName !== ""
            anchors.verticalCenter: parent.verticalCenter

            DIcon {
                anchors.centerIn: parent
                name: root.iconName
                size: Style.iconSize
                color: root.contentColor
                visible: !root.busy
            }

            Loader {
                anchors.fill: parent
                active: root.busy
                sourceComponent: DSpinner {
                    size: Style.iconSize
                    strokeWidth: Style.outlineWidthFocused
                    color: root.contentColor
                    running: root.busy && root.visible
                    Accessible.ignored: true
                }
            }
        }

        StyledText {
            visible: root.extended
            text: root.text
            font.pixelSize: Style.fontSizeMedium
            font.weight: Style.fontWeightMedium
            color: root.contentColor
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
