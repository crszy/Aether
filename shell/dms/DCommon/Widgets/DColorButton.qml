import QtQuick
import qs.dms.DCommon.Common

StyledButton {
    id: root

    property color swatchColor: "transparent"
    property bool selected: false
    property var tooltipText: null

    implicitWidth: Style.iconButtonSize
    implicitHeight: implicitWidth
    radius: Style.fullRadius(width, height)
    Accessible.name: tooltipText || swatchColor.toString()
    Accessible.role: Accessible.RadioButton
    Accessible.checkable: true
    Accessible.checked: selected
    Accessible.onToggleAction: {
        if (enabled)
            click();
    }

    DColorSwatch {
        anchors.centerIn: parent
        width: Math.min(root.width, root.height) - Style.spacingS
        height: width
        swatchColor: root.swatchColor
        minPreviewAlpha: 0
        ringColor: Style.outlineVariant
        visible: root.enabled
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(root.width, root.height) - Style.spacingS
        height: width
        radius: Style.fullRadius(width, height)
        color: Style.onSurface_12
        visible: !root.enabled
    }

    Rectangle {
        anchors.centerIn: parent
        width: Style.iconSizeSmall + Style.outlineWidthFocused * 2
        height: width
        radius: Style.fullRadius(width, height)
        color: root.enabled ? Style.primaryContainer : "transparent"
        visible: root.selected

        DIcon {
            anchors.centerIn: parent
            name: "check"
            size: Style.iconSizeSmall
            color: root.enabled ? Style.onPrimaryContainer : Style.onSurface_38
        }
    }

    StateLayer {
        control: root
        disabled: !root.enabled
        stateColor: Style.onSurface
        tooltipText: root.tooltipText || root.swatchColor.toString()
    }

    FocusRing {
        visible: root.visualFocus
    }
}
