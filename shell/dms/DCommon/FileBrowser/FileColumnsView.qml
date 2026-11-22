pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets
import "FileColumnsLayout.js" as Layout
import "FilePreview.js" as FilePreview

Item {
    id: columns

    required property var body

    readonly property var pool: body.directoryPool
    readonly property bool mirrored: columns.LayoutMirroring.enabled
    readonly property string activePath: body.directory.path
    readonly property var ancestors: Layout.chainFor(activePath, body.homePath, FilePaths.parentOf)
    readonly property var chain: ancestors.concat([activePath])
    readonly property int activeDepth: ancestors.length
    readonly property var picked: body.selected
    readonly property var pickedEntry: picked.length === 1 ? picked[0] : null
    readonly property string lookaheadPath: pickedEntry?.isDir ? pickedEntry.path : ""
    readonly property bool trailingPane: picked.length > 1 || (pickedEntry !== null && !pickedEntry.isDir)
    readonly property real fittedWidth: Math.max(FileBrowserMetrics.columnMinPaneWidth, Layout.columnWidth(strip.width, FileBrowserMetrics.previewPaneWidth, FileBrowserMetrics.columnMinPaneWidth, FileBrowserMetrics.columnPaneWidth) * body.columnZoomFactor)
    readonly property real columnWidth: body.columnPaneWidth > 0 ? Math.max(FileBrowserMetrics.columnMinPaneWidth, Math.min(FileBrowserMetrics.columnPaneMaxWidth, body.columnPaneWidth)) : fittedWidth
    readonly property real activeEnd: (activeDepth + 1) * columnWidth
    readonly property real trailingWidth: slotEnd - activeEnd
    readonly property var activeList: activeColumn?.list ?? null
    readonly property int columnsPerRow: 1
    readonly property real rowPitch: activeList?.rowPitch ?? 0
    readonly property real originY: activeList?.originY ?? 0
    readonly property real contentY: activeList?.contentY ?? 0
    readonly property real contentHeight: activeList?.contentHeight ?? 0
    readonly property var headerItem: null

    property real stripEnd: 0
    property real slotEnd: 0
    property var activeColumn: null
    property var heldPaths: []
    property var _columns: []
    property int _stacking: 0
    property bool _jump: false

    function currentList() {
        const path = body.directory.path;
        return (columnFor(path, depthOf(path)) ?? activeColumn)?.list ?? null;
    }

    function positionViewAtIndex(index, mode) {
        currentList()?.positionViewAtIndex(index, mode);
    }

    function positionViewAtBeginning() {
        currentList()?.positionViewAtBeginning();
    }

    function revealIndex(index) {
        currentList()?.revealIndex(index);
    }

    function itemAtIndex(index) {
        return currentList()?.itemAtIndex(index) ?? null;
    }

    function visibleRange() {
        return currentList()?.visibleRange() ?? [0, -1];
    }

    function logicalX(depth) {
        return depth * columnWidth;
    }

    function physicalX(logical, width) {
        return mirrored ? stripEnd - logical - width : logical;
    }

    function physicalContentX(logical) {
        return mirrored ? Math.max(0, stripEnd - strip.width - logical) : logical;
    }

    function logicalContentX() {
        return mirrored ? Math.max(0, stripEnd - strip.width - strip.contentX) : strip.contentX;
    }

    function depthOf(path) {
        return Layout.chainFor(path, body.homePath, FilePaths.parentOf).length;
    }

    function revealActive(depth, animate) {
        const previous = logicalContentX();
        const target = Layout.revealTarget(previous, depth, columnWidth, FileBrowserMetrics.previewPaneWidth, strip.width);
        scrollAnim.stop();
        slotEnd = target.stripEnd;
        stripEnd = Math.max(slotEnd, previous + strip.width);
        strip.contentX = physicalContentX(previous);
        const to = physicalContentX(target.contentX);
        if (!animate || !FileBrowserMetrics.animationsEnabled || to === strip.contentX) {
            strip.contentX = to;
            settleStrip();
            return;
        }
        scrollAnim.to = to;
        scrollAnim.restart();
    }

    function settleStrip() {
        const logical = logicalContentX();
        stripEnd = Math.max(slotEnd, logical + strip.width);
        strip.contentX = physicalContentX(logical);
    }

    function columnFor(path, depth) {
        return _columns.find(column => !column.leaving && column.dirPath === path && column.depth === depth) ?? null;
    }

    function ensureColumn(path, depth, entrance) {
        const open = columnFor(path, depth);
        if (open)
            return open;
        const parked = _columns.find(column => column.leaving && column.dirPath === path && column.depth === depth);
        if (parked) {
            parked.revive();
            return parked;
        }
        const replacing = _columns.some(column => !column.leaving && column.depth === depth);
        const column = columnComponent.createObject(lane, {
            "dirPath": path,
            "depth": depth,
            "model": pool.modelFor(path),
            "entrance": entrance ?? (replacing ? "fade" : "slide"),
            "z": ++_stacking
        });
        _columns = _columns.concat([column]);
        heldPaths = _columns.map(item => item.dirPath);
        return column;
    }

    function remove(column) {
        _columns = _columns.filter(item => item !== column);
        heldPaths = _columns.map(item => item.dirPath);
        if (activeColumn === column)
            activeColumn = null;
        column.destroy();
    }

    function columnShown(column) {
        for (const other of _columns.slice()) {
            if (other.leaving && other.exit === "replaced" && other.depth === column.depth)
                remove(other);
        }
    }

    function wantedColumns() {
        const out = chain.map((path, depth) => ({
                    "path": path,
                    "depth": depth
                })).filter(item => item.path !== "");
        if (lookaheadPath !== "")
            out.push({
                "path": lookaheadPath,
                "depth": chain.length
            });
        return out;
    }

    function syncColumns() {
        const wanted = wantedColumns();
        const keep = new Set(wanted.map(item => item.path + "\n" + item.depth));
        for (const column of _columns.slice()) {
            if (column.leaving || keep.has(column.dirPath + "\n" + column.depth))
                continue;
            const replacement = _columns.find(other => other !== column && !other.leaving && other.depth === column.depth);
            if (_jump) {
                column.leave("none");
                continue;
            }
            if (replacement) {
                column.leave(replacement.shown ? "none" : "replaced");
                continue;
            }
            column.leave("close");
        }
        for (const item of wanted) {
            if (!columnFor(item.path, item.depth))
                ensureColumn(item.path, item.depth, _jump ? "none" : undefined);
        }
        for (const column of _columns.slice()) {
            if (column.exit === "replaced" && !_columns.some(other => !other.leaving && other.depth === column.depth))
                column.leave("close");
        }
        _jump = false;
        activeColumn = columnFor(activePath, activeDepth);
    }

    onHeldPathsChanged: pool.heldPaths = heldPaths
    onActivePathChanged: {
        const depth = depthOf(activePath);
        _jump = columnFor(activePath, depth) === null;
        activeColumn = activePath === "" ? null : ensureColumn(activePath, depth, _jump ? "none" : undefined);
        revealActive(depth, !_jump);
        sync.restart();
    }
    onLookaheadPathChanged: {
        if (lookaheadPath !== "")
            ensureColumn(lookaheadPath, depthOf(body.directory.path) + 1);
        sync.restart();
    }
    Component.onCompleted: {
        _jump = true;
        activeColumn = activePath === "" ? null : ensureColumn(activePath, activeDepth, "none");
        revealActive(activeDepth, false);
        syncColumns();
    }
    Component.onDestruction: pool.heldPaths = []

    Timer {
        id: sync

        interval: 0
        onTriggered: columns.syncColumns()
    }

    Component {
        id: columnComponent

        Item {
            id: column

            required property string dirPath
            required property int depth
            required property var model
            property string entrance: "slide"
            property bool leaving: false
            property string exit: ""
            property bool shown: false
            property bool slowListing: false
            property real presence: 0
            property real span: 1
            property real slide: entrance === "slide" ? (columns.mirrored ? FileBrowserMetrics.columnSlideDistance : -FileBrowserMetrics.columnSlideDistance) : 0

            readonly property bool active: !leaving && columns.activePath === dirPath && columns.activeDepth === depth
            readonly property string childPath: active || leaving || columns.chain[depth] !== dirPath ? "" : (columns.chain[depth + 1] ?? "")
            readonly property bool listing: model.loading && model.count === 0
            readonly property SelectionModel ownSelection: SelectionModel {}
            readonly property alias list: list

            function entryAt(index) {
                return index >= 0 && index < model.count ? model.entries.get(index) : null;
            }

            function syncChild() {
                if (childPath === "") {
                    ownSelection.clear();
                    return;
                }
                ownSelection.select(childPath);
                const index = model.indexOfPath(childPath);
                if (index >= 0)
                    list.revealIndex(index);
            }

            function requestVisible() {
                settle.stop();
                const range = list.visibleRange();
                model.requestThumbnails(range[0], range[1]);
                if (range[1] >= model.count - 1)
                    model.loadMore();
            }

            function leave(mode) {
                leaving = true;
                exit = mode;
                grace.stop();
                switch (mode) {
                case "replaced":
                    return;
                case "close":
                    if (FileBrowserMetrics.animationsEnabled && shown) {
                        closeAnim.restart();
                        return;
                    }
                    break;
                }
                columns.remove(column);
            }

            function revive() {
                closeAnim.stop();
                leaving = false;
                exit = "";
                entrance = "fade";
                span = 1;
                presence = shown ? 1 : 0;
            }

            x: columns.physicalX(columns.logicalX(depth), width)
            width: columns.columnWidth * span
            height: lane.height
            clip: true
            enabled: !leaving
            opacity: presence
            transform: Translate {
                x: column.slide
            }

            Behavior on presence {
                enabled: FileBrowserMetrics.animationsEnabled && column.entrance !== "none" && !column.leaving
                DAnim {
                    duration: Style.expressiveDurations.expressiveEffects
                    easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                }
            }

            Behavior on slide {
                enabled: FileBrowserMetrics.animationsEnabled && !column.leaving
                DAnim {
                    duration: Style.expressiveDurations.expressiveFastSpatial
                }
            }

            Behavior on span {
                enabled: FileBrowserMetrics.animationsEnabled && !column.leaving
                DAnim {
                    duration: Style.expressiveDurations.expressiveFastSpatial
                }
            }

            SequentialAnimation {
                id: closeAnim

                ParallelAnimation {
                    DAnim {
                        target: column
                        property: "span"
                        to: 0
                        duration: Style.expressiveDurations.expressiveFastSpatial
                    }

                    DAnim {
                        target: column
                        property: "presence"
                        to: 0
                        duration: Style.expressiveDurations.expressiveEffects
                        easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                    }
                }

                ScriptAction {
                    script: columns.remove(column)
                }
            }

            Timer {
                id: grace

                interval: FileBrowserMetrics.skeletonDelay
                onTriggered: {
                    column.slowListing = column.listing;
                    column.shown = true;
                }
            }

            Timer {
                id: settle

                interval: FileBrowserMetrics.settleInterval
                onTriggered: column.requestVisible()
            }

            Connections {
                target: column.model

                function onGone() {
                    columns.remove(column);
                }

                function onListed() {
                    if (column.leaving)
                        return;
                    column.syncChild();
                    column.shown = true;
                    column.requestVisible();
                }

                function onLoadingChanged() {
                    if (!column.model.loading)
                        column.shown = true;
                }
            }

            onChildPathChanged: syncChild()
            onListingChanged: {
                if (listing) {
                    grace.restart();
                    return;
                }
                grace.stop();
                slowListing = false;
            }
            onShownChanged: {
                if (!shown || leaving)
                    return;
                presence = 1;
                slide = 0;
                columns.columnShown(column);
            }
            Component.onCompleted: {
                syncChild();
                if (listing) {
                    grace.start();
                    return;
                }
                shown = true;
                requestVisible();
            }

            Rectangle {
                anchors.fill: parent
                color: columns.body.surfaceColor
            }

            TapHandler {
                enabled: !column.active && !column.leaving
                acceptedButtons: Qt.LeftButton
                onTapped: columns.body.openRequested(column.dirPath)
            }

            FileDropArea {
                anchors.fill: parent
                dropPath: column.dirPath
                enabled: !column.active && !column.leaving && columns.body.dropEnabled
                scrollView: list
                onDropped: drop => columns.body.itemsDropped(column.dirPath, drop)
            }

            FileListView {
                id: list

                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                width: columns.columnWidth - Style.dividerWidth
                selection: column.active ? columns.body.selection : column.ownSelection
                iconSize: columns.body.iconSize
                zoom: columns.body.zoom
                renamingPath: column.active ? columns.body.renamingPath : ""
                cutSet: columns.body.cutSet
                columnIds: []
                chevrons: true
                singleClickActivates: column.active && columns.body.singleClickActivates
                nameHighlight: column.active ? columns.body.nameHighlight : ""
                multiSelect: column.active && columns.body.multiSelect
                dragEnabled: column.active && columns.body.dragEnabled
                dropEnabled: columns.body.dropEnabled && !column.leaving
                model: column.model.entries
                onItemClicked: (index, modifiers) => {
                    if (column.active) {
                        columns.body.click(index, modifiers);
                        return;
                    }
                    const entry = column.entryAt(index);
                    if (entry)
                        columns.body.revealRequested(entry.path);
                }
                onItemActivated: index => {
                    if (column.active) {
                        columns.body.activate(index);
                        return;
                    }
                    const entry = column.entryAt(index);
                    if (!entry)
                        return;
                    if (entry.isDir) {
                        columns.body.openRequested(entry.path);
                        return;
                    }
                    columns.body.activateRequested(entry);
                }
                onItemMiddleClicked: index => {
                    const entry = column.entryAt(index);
                    if (entry?.isDir)
                        columns.body.tabRequested(entry.path);
                }
                onItemContextMenu: (index, pointX, pointY, modifiers) => {
                    if (!column.active) {
                        const entry = column.entryAt(index);
                        if (!entry)
                            return;
                        columns.body.revealRequested(entry.path);
                    }
                    columns.body.forwardItemMenu(list, index, pointX, pointY, modifiers);
                }
                onItemDragStarted: index => {
                    if (column.active)
                        columns.body.startDrag(index);
                }
                onItemDropped: (index, drop) => {
                    const entry = column.entryAt(index);
                    if (entry)
                        columns.body.itemsDropped(entry.path, drop);
                }
                onItemSpringLoaded: index => {
                    const entry = column.entryAt(index);
                    if (entry?.isDir)
                        columns.body.openRequested(entry.path);
                }
                onRenameSubmitted: (target, name) => columns.body.finishRename(target, name)
                onRenameCancelled: columns.body.finishRename("", "")
                onContentYChanged: settle.restart()
                onCountChanged: settle.restart()

                Loader {
                    active: column.active
                    sourceComponent: FileViewBackground {
                        view: list
                        selection: columns.body.selection
                        dropPath: column.dirPath
                        onCleared: {
                            columns.body.focusRequested();
                            columns.body.selection.clear();
                        }
                        onContextMenuRequested: (pointX, pointY) => {
                            const point = list.mapToItem(columns.body, pointX, pointY);
                            columns.body.backgroundMenuRequested(point.x, point.y);
                        }
                        onDropped: drop => columns.body.itemsDropped(column.dirPath, drop)
                    }
                }
            }

            LoadingSkeleton {
                anchors.fill: list
                visible: column.slowListing
                rowHeight: list.rowHeight
            }

            StyledText {
                anchors.centerIn: list
                width: list.width - Style.spacingL * 2
                visible: !column.model.loading && column.model.count === 0
                text: column.model.error !== "" ? FileFormat.listingError(column.model.errorCode) : I18n.tr("This folder is empty", "empty directory placeholder")
                color: Style.surfaceVariantText
                font.pixelSize: Style.fontSizeMedium
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Style.dividerWidth
                color: divider.containsMouse || divider.pressed ? Style.primary : Style.outlineVariant
            }

            MouseArea {
                id: divider

                property real pressX: 0
                property real pressWidth: 0

                z: 10
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Style.spacingS
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.SizeHorCursor
                acceptedButtons: Qt.LeftButton
                onPressed: mouse => {
                    pressX = mapToItem(null, mouse.x, 0).x;
                    pressWidth = columns.columnWidth;
                }
                onPositionChanged: mouse => {
                    if (!pressed)
                        return;
                    const delta = (mapToItem(null, mouse.x, 0).x - pressX) / (column.depth + 1);
                    columns.body.columnPaneResizeRequested(pressWidth + (columns.mirrored ? -delta : delta));
                }
            }
        }
    }

    DFlickable {
        id: strip

        anchors.fill: parent
        contentWidth: columns.stripEnd
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        clip: true
        onWidthChanged: columns.revealActive(columns.activeDepth, false)
        onDragStarted: scrollAnim.stop()

        ScrollBar.horizontal: DHorizontalScrollbar {
            targetFlickable: strip
            onPressedChanged: scrollAnim.stop()
        }

        DAnim {
            id: scrollAnim

            target: strip
            property: "contentX"
            duration: Style.expressiveDurations.expressiveFastSpatial
            onRunningChanged: {
                if (!running)
                    columns.settleStrip();
            }
        }

        Item {
            id: lane

            width: columns.stripEnd
            height: strip.height

            Loader {
                x: columns.physicalX(columns.activeEnd + FileBrowserMetrics.contentSpacing, width)
                width: Math.max(0, columns.trailingWidth - FileBrowserMetrics.contentSpacing)
                height: lane.height
                active: columns.body.previewOpen
                visible: columns.trailingPane && (item?.shownEntries.length ?? 0) > 0
                sourceComponent: FilePreviewPane {
                    entries: {
                        if (!columns.trailingPane)
                            return [];
                        const entry = columns.body.previewEntry();
                        return entry ? [entry] : [];
                    }
                    sourceModel: columns.body.directory.entries
                    backend: columns.body.directory.backend
                    imageSizeLimit: columns.body.directory.thumbnailSizeLimit
                    includeHidden: columns.body.showHidden
                    readsAllowed: FilePreview.readsAllowed(columns.body.directory)
                    thumbnailsEnabled: columns.body.directory.thumbnailsEnabled
                    chipColor: columns.body.chipColor
                    closable: columns.body.previewKeys
                    onCloseRequested: columns.body.previewKeyRequested("toggle", 0)
                }
            }
        }
    }
}
