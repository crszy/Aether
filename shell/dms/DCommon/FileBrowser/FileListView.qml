pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

DListView {
    id: list

    property SelectionModel selection: null
    property real iconSize: Style.iconSize
    property int zoom: FileBrowserMetrics.defaultListZoom
    property string renamingPath: ""
    property var cutSet: ({})
    property var childCounts: ({})
    property var columnIds: []
    property var storedWidths: ({})
    property bool dragEnabled: true
    property bool dropEnabled: true
    property bool multiSelect: true
    property bool singleClickActivates: false
    property bool chevrons: false
    property string nameHighlight: ""

    readonly property int columnsPerRow: 1
    readonly property bool compact: zoom <= 0
    readonly property real rowHeight: FileBrowserMetrics.listRowHeightFor(iconSize, compact)
    readonly property real rowPitch: rowHeight + spacing
    readonly property real contentOrigin: headerItem ? headerItem.y + headerItem.height : originY

    signal itemClicked(int index, int modifiers)
    signal itemActivated(int index)
    signal itemMiddleClicked(int index)
    signal itemContextMenu(int index, real pointX, real pointY, int modifiers)
    signal itemDragStarted(int index)
    signal itemDropped(int index, var drop)
    signal itemSpringLoaded(int index)
    signal renameSubmitted(string path, string name)
    signal renameCancelled

    function columnWidth(id) {
        return FileColumns.widthFor(id, storedWidths);
    }

    function visibleRange() {
        const first = Math.floor((contentY - contentOrigin) / rowPitch);
        const last = Math.floor((contentY - contentOrigin + height) / rowPitch);
        return [Math.max(0, first), Math.min(count - 1, last)];
    }

    function indicesIn(rect) {
        const first = Math.max(0, Math.floor((rect.y - contentOrigin) / rowPitch));
        const last = Math.min(count - 1, Math.floor((rect.y + rect.height - contentOrigin) / rowPitch));
        const out = [];
        for (let i = first; i <= last; i++)
            out.push(i);
        return out;
    }

    clip: true
    keyNavigationEnabled: false
    activeFocusOnTab: false
    spacing: Style.groupedListGap
    cacheBuffer: Math.max(0, height)
    currentIndex: -1
    reuseItems: false
    add: null

    delegate: FileRow {
        view: list
    }
}
