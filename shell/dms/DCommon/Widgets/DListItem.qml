import QtQuick
import qs.dms.DCommon.Common

StyledButton {
    id: root

    property bool isSelected: false
    property color surfaceColor: Style.foregroundColor(Style.cardSurface, Style.isFloatingWindow(root))
    property bool firstInGroup: true
    property bool lastInGroup: true
    property Item listView: ListView.view
    property bool externalHighlight: listView?.highlightSelection ?? false
    property var keyForwardTargets: []
    property bool _pooled: false
    property bool isHovered: hovered
    property alias tooltipText: stateLayer.tooltipText
    property color idleContentColor: Style.onSurface
    readonly property color contentColor: colorForRole(idleContentColor)
    readonly property color supportingContentColor: colorForRole(Style.onSurfaceVariant)

    function colorForRole(idleColor) {
        return !enabled ? Style.onSurface_38 : isSelected || (!externalHighlight && visualFocus) ? Style.onSelectedContainer : idleColor;
    }

    signal contextMenuRequested(real mouseX, real mouseY)
    signal pointerMoved

    Keys.forwardTo: keyForwardTargets
    ListView.onPooled: _pooled = true
    ListView.onReused: {
        _pooled = false;
        visible = true;
        opacity = 1;
    }
    z: 1

    implicitHeight: Style.listItemHeight
    // a list view moves between its rows with the arrows; rows laid out by hand are only reachable with Tab
    focusPolicy: activeFocus || isSelected || listView === null ? Qt.StrongFocus : Qt.ClickFocus
    Accessible.role: Accessible.ListItem
    Accessible.selected: isSelected
    color: externalHighlight ? "transparent" : isSelected || visualFocus ? Style.selectedContainer : root.surfaceColor
    border.width: externalHighlight ? 0 : Style.layerOutlineWidth
    border.color: Style.outlineMedium
    radius: Style.groupedListInnerRadius
    topLeftRadius: firstInGroup ? Style.groupedListOuterRadius : radius
    topRightRadius: topLeftRadius
    bottomLeftRadius: lastInGroup ? Style.groupedListOuterRadius : radius
    bottomRightRadius: bottomLeftRadius

    Loader {
        active: root.listView?.highlightSelection ?? false
        sourceComponent: Rectangle {
            parent: root.listView.contentItem
            z: -1
            x: root.x
            y: root.y
            width: root.width
            height: root.height
            visible: root.visible && !root._pooled
            opacity: root.opacity
            color: root.surfaceColor
            border.width: Style.layerOutlineWidth
            border.color: Style.outlineMedium
            topLeftRadius: root.topLeftRadius
            topRightRadius: root.topRightRadius
            bottomLeftRadius: root.bottomLeftRadius
            bottomRightRadius: root.bottomRightRadius
        }
    }

    StateLayer {
        id: stateLayer
        control: root
        disabled: !root.enabled
        hovered: root.isHovered
        stateColor: root.contentColor
        onPositionChanged: root.pointerMoved()
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => root.contextMenuRequested(eventPoint.position.x, eventPoint.position.y)
    }
}
