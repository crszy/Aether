pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

StyledButton {
    id: surface

    required property int index
    required property string path
    required property bool isDir

    property Item view: null
    property bool selected: false
    property bool cursor: false
    property bool cut: false
    property bool renaming: false
    property var hitTargets: []
    property real contentRadius: Style.groupedListInnerRadius

    readonly property bool dropTarget: isDir && !renaming && dropZone.containsDrag
    readonly property color contentColor: selected ? Style.onSelectedContainer : dropTarget ? Style.onPrimaryContainer : Style.surfaceText
    readonly property color supportingColor: selected ? Style.onSelectedContainer : Style.surfaceVariantText

    default property alias content: holder.data

    function containsContent(pointX, pointY) {
        if (renaming || hitTargets.length === 0)
            return true;
        const padding = Style.spacingXS;
        for (const target of hitTargets) {
            if (!target.visible || target.width <= 0 || target.height <= 0)
                continue;
            const point = target.mapFromItem(surface, pointX, pointY);
            const width = Math.min(target.width, target.contentWidth ?? target.width);
            const height = Math.min(target.height, target.contentHeight ?? target.height);
            let left = 0;
            switch (target.effectiveHorizontalAlignment) {
            case Text.AlignHCenter:
                left = (target.width - width) / 2;
                break;
            case Text.AlignRight:
                left = target.width - width;
                break;
            }
            const top = target.verticalAlignment === Text.AlignVCenter ? (target.height - height) / 2 : 0;
            if (point.x >= left - padding && point.x <= left + width + padding && point.y >= top - padding && point.y <= top + height + padding)
                return true;
        }
        return false;
    }

    focusPolicy: Qt.NoFocus
    radius: contentRadius
    color: selected ? Style.selectedContainer : dropTarget ? Style.primaryContainer : "transparent"
    Accessible.name: path.substring(path.lastIndexOf("/") + 1)
    Accessible.selected: selected

    // Inset, not outset: the views clip, so an outset ring loses its side strokes.
    Rectangle {
        anchors.fill: parent
        anchors.margins: Style.focusRingOffset
        visible: surface.cursor && !surface.renaming
        radius: Math.max(0, parent.radius - Style.focusRingOffset)
        color: "transparent"
        border.width: Style.focusRingWidth
        border.color: Style.focusRingColor
    }

    HoverHandler {
        enabled: surface.view?.singleClickActivates === true && !surface.renaming
        cursorShape: Qt.PointingHandCursor
    }

    StateLayer {
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        enabled: !surface.renaming
        hovered: surface.hovered
        stateColor: surface.contentColor
        cornerRadius: surface.radius

        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) {
                const point = mapToItem(surface.view, mouse.x, mouse.y);
                surface.view.itemContextMenu(surface.index, point.x, point.y, mouse.modifiers);
                return;
            }
            surface.view.itemClicked(surface.index, mouse.modifiers);
        }

        onDoubleClicked: mouse => {
            if (mouse.button !== Qt.LeftButton || surface.view?.singleClickActivates === true)
                return;
            surface.view.itemActivated(surface.index);
        }
    }

    TapHandler {
        acceptedButtons: Qt.MiddleButton
        enabled: !surface.renaming
        onTapped: surface.view.itemMiddleClicked(surface.index)
    }

    DragHandler {
        target: null
        enabled: !surface.renaming && surface.view?.dragEnabled === true
        onActiveChanged: {
            if (!active)
                return;
            surface.view.itemDragStarted(surface.index);
        }
    }

    FileDropArea {
        id: dropZone

        anchors.fill: parent
        dropPath: surface.path
        enabled: surface.isDir && !surface.renaming && surface.view?.dropEnabled === true
        springLoad: surface.isDir && !surface.selected
        onDropped: drop => surface.view.itemDropped(surface.index, drop)
        onSpringLoaded: surface.view.itemSpringLoaded(surface.index)
    }

    Item {
        id: holder

        anchors.fill: parent
    }
}
