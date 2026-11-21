import QtQuick
import QtQuick.Layouts
import qs.dms.DCommon.Common

ColumnLayout {
    id: root

    required property string title
    property string description: ""
    property bool expanded: false
    property bool showBackground: false
    property alias headerColor: headerRect.color

    function toggle() {
        if (!enabled)
            return;
        toggleRequested();
        expanded = !expanded;
    }

    signal toggleRequested

    spacing: Style.groupedListGap
    Layout.fillWidth: true

    StyledButton {
        id: headerRect
        Accessible.role: Accessible.Button
        Accessible.name: root.title
        Accessible.description: root.description
        checkable: true
        checked: root.expanded
        onClicked: root.toggle()

        FocusRing {
            visible: headerRect.visualFocus
            radius: Style.groupedListOuterRadius + Style.focusRingOffset
        }
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(titleRow.implicitHeight + Style.spacingM * 2, Style.listItemHeight)
        topLeftRadius: Style.groupedListOuterRadius
        topRightRadius: Style.groupedListOuterRadius
        bottomLeftRadius: root.expanded ? Style.groupedListInnerRadius : Style.groupedListOuterRadius
        bottomRightRadius: root.expanded ? Style.groupedListInnerRadius : Style.groupedListOuterRadius
        color: Style.foregroundColor(Style.cardSurface, Style.isFloatingWindow(root))
        border.width: Style.layerOutlineWidth
        border.color: Style.outlineMedium

        Behavior on bottomLeftRadius {
            enabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
            DAnim {
                duration: Style.expressiveDurations.expressiveFastSpatial
                easing.bezierCurve: Style.expressiveCurves.standard
            }
        }

        Behavior on bottomRightRadius {
            enabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
            DAnim {
                duration: Style.expressiveDurations.expressiveFastSpatial
                easing.bezierCurve: Style.expressiveCurves.standard
            }
        }

        RowLayout {
            id: titleRow
            anchors.fill: parent
            anchors.leftMargin: Style.spacingL
            anchors.rightMargin: Style.spacingL
            spacing: Style.spacingM

            StyledText {
                text: root.title
                font.pixelSize: Style.fontSizeMedium
                font.weight: Style.fontWeightMedium
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            DIcon {
                name: "expand_more"
                size: Style.iconSize
                color: Style.onSurfaceVariant
                rotation: root.expanded ? 180 : 0

                Behavior on rotation {
                    enabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                    DAnim {
                        duration: Style.expressiveDurations.expressiveFastSpatial
                        easing.bezierCurve: Style.expressiveCurves.expressiveDefaultSpatial
                    }
                }
            }
        }

        StateLayer {
            control: headerRect
            anchors.fill: parent
            disabled: !root.enabled
        }
    }

    default property alias content: contentColumn.data

    Item {
        id: contentWrapper
        visible: root.expanded || height > 0
        enabled: root.expanded
        Layout.fillWidth: true
        Layout.preferredHeight: root.expanded ? (contentColumn.implicitHeight + Style.spacingM * 2) : 0
        clip: true

        Behavior on Layout.preferredHeight {
            enabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
            DAnim {
                duration: Style.expressiveDurations.expressiveDefaultSpatial
                easing.bezierCurve: Style.expressiveCurves.standard
            }
        }

        Rectangle {
            id: backgroundRect
            anchors.fill: parent
            topLeftRadius: Style.groupedListInnerRadius
            topRightRadius: Style.groupedListInnerRadius
            bottomLeftRadius: Style.groupedListOuterRadius
            bottomRightRadius: Style.groupedListOuterRadius
            color: Style.foregroundColor(Style.cardSurface, Style.isFloatingWindow(root))
            border.width: Style.layerOutlineWidth
            border.color: Style.outlineMedium
            opacity: root.showBackground && root.expanded ? 1.0 : 0.0
            visible: root.showBackground

            Behavior on opacity {
                enabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                DAnim {
                    duration: Style.expressiveDurations.expressiveEffects
                    easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                }
            }
        }

        ColumnLayout {
            id: contentColumn
            anchors.left: parent.left
            anchors.right: parent.right
            y: Style.spacingM
            anchors.leftMargin: Style.spacingL
            anchors.rightMargin: Style.spacingL
            spacing: Style.spacingS
            opacity: root.expanded ? 1.0 : 0.0

            Behavior on opacity {
                enabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                DAnim {
                    duration: Style.expressiveDurations.expressiveEffects
                    easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                }
            }

            StyledText {
                id: descriptionText
                Layout.fillWidth: true
                Layout.topMargin: root.description !== "" ? Style.spacingXS : 0
                Layout.bottomMargin: root.description !== "" ? Style.spacingS : 0
                visible: root.description !== ""
                text: root.description
                color: Style.onSurfaceVariant
                font.pixelSize: Style.fontSizeSmall
                wrapMode: Text.Wrap
            }
        }
    }
}
