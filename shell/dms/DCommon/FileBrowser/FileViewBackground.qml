pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common

Item {
    id: background

    required property Item view
    required property SelectionModel selection
    property string dropPath: ""

    readonly property real scrollMargin: Math.min(FileBrowserMetrics.dragScrollEdge, view.height / 2)
    readonly property real scrollDirection: {
        if (!area.banding || !band.past)
            return 0;
        if (area.pointerPosition.y < scrollMargin)
            return -1;
        return area.pointerPosition.y > view.height - scrollMargin ? 1 : 0;
    }

    signal cleared
    signal contextMenuRequested(real pointX, real pointY)
    signal dropped(var drop)

    parent: view.contentItem
    z: -1
    y: view.originY
    width: view.width
    height: Math.max(view.contentHeight, view.height)

    MouseArea {
        id: area

        property var kept: []
        property bool banding: false
        property bool clearPending: false
        property bool secondClick: false
        property int pressedIndex: -1
        property point pointerPosition: Qt.point(0, 0)

        parent: background.view.contentItem
        x: background.x
        y: background.y
        width: background.width
        height: background.height
        z: 2
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true

        onPressed: mouse => {
            const contentY = mouse.y + background.y;
            const index = background.view.indexAt(mouse.x, contentY);
            const item = background.view.itemAtIndex(index);
            const point = item ? mapToItem(item, mouse.x, mouse.y) : null;
            if (contentY < background.view.contentOrigin || (item && item.containsContent(point.x, point.y))) {
                mouse.accepted = false;
                return;
            }
            pressedIndex = item?.contains(point) ? index : -1;
            secondClick = false;
            if (mouse.button === Qt.RightButton)
                return;
            band.originX = mouse.x;
            band.originY = mouse.y;
            band.pointX = mouse.x;
            band.pointY = mouse.y;
            kept = background.view.multiSelect && (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0 ? background.selection.paths.slice() : [];
            clearPending = kept.length === 0;
            if (pressedIndex < 0)
                background.applyPendingClear();
            if (!background.view.multiSelect)
                return;
            background.selection.keyboardCursor = false;
            background.view.stopMomentum();
            pointerPosition = mapToItem(background.view, mouse.x, mouse.y);
            banding = true;
        }

        onPositionChanged: mouse => {
            if (!banding)
                return;
            pointerPosition = mapToItem(background.view, mouse.x, mouse.y);
            background.updateSelection();
        }

        onReleased: mouse => {
            banding = false;
            if (mouse.button !== Qt.LeftButton || secondClick || pressedIndex < 0 || band.past)
                return;
            background.view.itemClicked(pressedIndex, mouse.modifiers);
        }

        onCanceled: banding = false

        onDoubleClicked: mouse => {
            secondClick = true;
            if (mouse.button !== Qt.LeftButton || pressedIndex < 0 || background.view.singleClickActivates)
                return;
            background.view.itemActivated(pressedIndex);
        }

        onClicked: mouse => {
            if (mouse.button !== Qt.RightButton)
                return;
            const point = mapToItem(background.view, mouse.x, mouse.y);
            if (pressedIndex >= 0) {
                background.view.itemContextMenu(pressedIndex, point.x, point.y, mouse.modifiers);
                return;
            }
            background.contextMenuRequested(point.x, point.y);
        }
    }

    function applyPendingClear() {
        if (!area.clearPending)
            return;
        area.clearPending = false;
        cleared();
    }

    function updateSelection() {
        const point = mapFromItem(view, area.pointerPosition.x, area.pointerPosition.y);
        band.pointX = point.x;
        band.pointY = point.y;
        if (!band.past)
            return;
        applyPendingClear();
        const rect = Qt.rect(band.x, band.y, band.width, band.height);
        const banded = view.indicesIn(rect).map(index => view.model.get(index).path);
        const fresh = banded.filter(path => !area.kept.includes(path));
        selection.replace(area.kept.concat(fresh), banded.length > 0 ? banded[banded.length - 1] : "");
    }

    RubberBand {
        id: band

        parent: background.view.contentItem
        y: background.y + Math.min(originY, pointY)
        z: 3
        visible: area.banding && past
    }

    ViewAutoScroll {
        target: background.view
        direction: background.scrollDirection
        onScrolled: background.updateSelection()
    }

    FileDropArea {
        anchors.fill: parent
        dropPath: background.dropPath
        enabled: background.view.dropEnabled === true
        onDropped: drop => background.dropped(drop)
    }
}
