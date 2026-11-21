pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

Popup {
    id: root

    property Item anchorItem: parent
    property Item highlightedItem: null
    default property alias rows: column.children

    readonly property bool motionEnabled: FileBrowserMetrics.animationsEnabled

    function openAt(pointX, pointY) {
        x = pointX;
        y = pointY;
        open();
    }

    function navigableItems() {
        const found = [];
        collectItems(column, found);
        return found;
    }

    function collectItems(item, out) {
        for (const child of item.children) {
            if (!child.visible)
                continue;
            if (child instanceof FileMenuItem) {
                if (child.enabled)
                    out.push(child);
                continue;
            }
            collectItems(child, out);
        }
    }

    function highlight(item) {
        if (highlightedItem)
            highlightedItem.highlighted = false;
        highlightedItem = item;
        if (!item)
            return;
        item.highlighted = true;
        const top = item.mapToItem(column, 0, 0).y;
        const bottom = top + item.height;
        if (top < flick.contentY) {
            flick.contentY = top;
            return;
        }
        if (bottom > flick.contentY + flick.height)
            flick.contentY = bottom - flick.height;
    }

    function moveHighlight(step) {
        const items = navigableItems();
        if (items.length === 0)
            return;
        const current = items.indexOf(highlightedItem);
        if (current < 0) {
            highlight(step > 0 ? items[0] : items[items.length - 1]);
            return;
        }
        highlight(items[(current + step + items.length) % items.length]);
    }

    function highlightEdge(last) {
        const items = navigableItems();
        if (items.length === 0)
            return;
        highlight(last ? items[items.length - 1] : items[0]);
    }

    width: FileBrowserMetrics.menuWidth
    padding: Style.spacingXS
    margins: 0
    modal: true
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: {
        highlight(null);
        flick.contentY = 0;
    }

    background: Item {
        ElevationShadow {
            anchors.fill: parent
            level: Style.elevationLevel2
            fallbackOffset: Style.spacingXS
            targetRadius: Style.cornerRadiusL
            targetColor: Style.surfaceContainer
            borderColor: Style.outlineMedium
            borderWidth: Style.layerOutlineWidth
            shadowEnabled: Style.elevationEnabled && Style.popoutElevationEnabled
        }
    }

    contentItem: DFlickable {
        id: flick

        implicitHeight: Math.min(column.height, Math.max(Style.menuItemHeight * 3, flick.Window.height - FileBrowserMetrics.menuMargin * 2))
        contentWidth: width
        contentHeight: column.height
        clip: true
        focus: true

        Keys.onPressed: event => {
            switch (event.key) {
            case Qt.Key_Down:
            case Qt.Key_J:
                root.moveHighlight(1);
                break;
            case Qt.Key_Up:
            case Qt.Key_K:
                root.moveHighlight(-1);
                break;
            case Qt.Key_Home:
                root.highlightEdge(false);
                break;
            case Qt.Key_End:
                root.highlightEdge(true);
                break;
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Space:
                if (!root.highlightedItem)
                    return;
                root.highlightedItem.click();
                break;
            default:
                return;
            }
            event.accepted = true;
        }

        HoverHandler {
            onPointChanged: {
                if (root.highlightedItem)
                    root.highlight(null);
            }
        }

        Column {
            id: column

            width: parent.width
            spacing: 0
        }
    }

    enter: Transition {
        enabled: root.motionEnabled

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
            easing.bezierCurve: Style.expressiveCurves.expressiveDefaultSpatial
        }
    }

    exit: Transition {
        enabled: root.motionEnabled

        DAnim {
            property: "opacity"
            from: 1
            to: 0
            duration: Style.shorterDuration
            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
        }
    }
}
