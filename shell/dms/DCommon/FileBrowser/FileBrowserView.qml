pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import "NameFilter.js" as NameFilter

FocusScope {
    id: root

    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    property var backend: Host.files
    property bool showSidebar: true
    property bool showNavigation: true
    property bool showPathBar: true
    property bool showHidden: false
    property var filters: []
    property string sortKey: "name"
    property bool sortDescending: false
    property string viewMode: "list"
    property int gridZoom: 1
    property int listZoom: 1
    property int columnZoom: FileBrowserMetrics.defaultColumnZoom
    property real columnPaneWidth: 0
    property var listColumns: ["size", "modified"]
    property var columnWidths: ({})
    property bool multiSelect: true
    property bool dragEnabled: false
    property bool dropEnabled: false
    property bool showSelectionFooter: multiSelect
    property bool previewDrawer: false
    property bool previewOpen: true
    property real previewWidth: 0
    property bool previewKeys: false
    property bool escapeClearsSelection: true
    property string emptyText: I18n.tr("This folder is empty", "empty directory placeholder")
    property Component header: null
    property Component pathBarTrailing: null
    property var cutSet: ({})
    property string nameFilter: ""
    property bool singleClickActivates: false
    property bool typeAheadEnabled: true
    property string typeAheadMode: "jump"
    property bool countFolders: true
    property bool dirsFirst: true
    property bool hiddenLast: true
    property bool thumbnailsEnabled: true
    property real thumbnailSizeLimit: -1
    property bool networkThumbnails: true
    property var folderJump: null
    property var unlistedPaths: []
    property color paneColor: "transparent"
    property color chipColor: Style.chipSurface
    property real paneRadius: FileBrowserMetrics.paneRadius
    property real paneMargin: FileBrowserMetrics.paneMargin
    property real paneMarginHorizontal: FileBrowserMetrics.paneMarginHorizontal
    property real paneBorderWidth: 0
    property color paneBorderColor: Style.primary
    default property alias toolbarItems: toolbarExtras.data

    readonly property alias path: history.path
    readonly property alias canGoBack: history.canGoBack
    readonly property alias canGoForward: history.canGoForward
    readonly property alias canGoUp: history.canGoUp
    readonly property bool editingPath: pathBar.editing || pathBar.connecting
    readonly property bool renaming: viewBody.renamingPath !== ""
    property int sidebarBreakpoint: FileBrowserMetrics.sidebarBreakpoint
    readonly property bool sidebarShown: showSidebar && width >= sidebarBreakpoint
    readonly property string homePath: backend?.userDirs?.find(dir => dir.key === "home")?.path ?? FilePaths.home
    readonly property alias selection: selectionModel
    readonly property var directory: pool.active ?? pool.empty
    readonly property alias body: viewBody
    readonly property var currentEntry: viewBody.cursorEntry()
    readonly property var selectedEntries: viewBody.selected

    property var _pendingReveal: []
    property bool _revealStarted: false
    property string _pendingRename: ""

    signal activateRequested(var entry)
    signal itemMenuRequested(int index, real pointX, real pointY, int modifiers)
    signal backgroundMenuRequested(real pointX, real pointY)
    signal columnsMenuRequested(real pointX, real pointY)
    signal itemsDropped(string dest, var drop)
    signal dragRequested(var paths)
    signal renameRequested(string target, string name)
    signal sortRequested(string key)
    signal zoomRequested(string viewMode, int level)
    signal columnResizeRequested(string id, real width)
    signal columnPaneResizeRequested(real width)
    signal previewToggleRequested
    signal previewResized(real width)
    signal focusRequested
    signal tabRequested(string path)
    signal pathMenuRequested(string path, real pointX, real pointY)
    signal locationRequested(var location)
    signal locationCancelled
    signal filterRequested(string text)
    signal filterFocusRequested
    signal navigating(string target)

    function navigate(target) {
        _pendingReveal = [];
        _pendingRename = "";
        _stamp();
        history.go(target);
    }

    function back() {
        _pendingReveal = [];
        _pendingRename = "";
        _stamp();
        history.back();
    }

    function forward() {
        _pendingReveal = [];
        _pendingRename = "";
        _stamp();
        history.forward();
    }

    function up() {
        _pendingReveal = [];
        _pendingRename = "";
        _stamp();
        history.up();
    }

    function reveal(targets) {
        _pendingRename = "";
        _reveal(targets);
    }

    function revealAndRename(target) {
        _pendingRename = target;
        _reveal([target]);
    }

    function _reveal(targets) {
        const paths = [].concat(targets);
        const parent = FilePaths.parentOf(paths[0] ?? "");
        if (parent === "")
            return;
        _stamp();
        selectionModel.clear();
        _revealStarted = false;
        _pendingReveal = paths.filter(path => FilePaths.parentOf(path) === parent);
        if (parent === history.path) {
            _applyPendingReveal();
            return;
        }
        history.go(parent);
    }

    function refresh() {
        directory.refresh();
    }

    function editPath() {
        pathBar.startEditing();
    }

    function startJump() {
        pathBar.startJump();
    }

    function dismissPathEditor(x, y) {
        if (!pathBar.editing || pathBar.connecting || pathBar.contains(pathBar.mapFromItem(root, x, y)))
            return;
        pathBar.stopEditing();
    }

    function settleLocation(target) {
        pathBar.settleLocation(target);
    }

    function focusBody() {
        viewBody.focusBody();
    }

    function beginRename(target) {
        viewBody.beginRename(target);
    }

    function saveState() {
        _stamp();
        return history.serialize();
    }

    function restoreState(state) {
        _pendingReveal = [];
        _pendingRename = "";
        history.restore(state);
    }

    function marker() {
        return {
            "contentY": viewBody.scrollOffset(),
            "cursor": selectionModel.cursorPath
        };
    }

    function _stamp() {
        history.marker = marker();
    }

    function _applyPendingReveal() {
        if (_pendingReveal.length === 0)
            return;
        const found = viewBody.reveal(_pendingReveal, _revealStarted);
        _revealStarted = _revealStarted || found.length > 0;
        _pendingReveal = _pendingReveal.filter(path => !found.includes(path));
        if (found.includes(_pendingRename)) {
            viewBody.beginRename(_pendingRename);
            _pendingRename = "";
        }
        if (_pendingReveal.length > 0)
            directory.loadMore();
    }

    onShowHiddenChanged: optionsSettle.restart()
    onSortKeyChanged: optionsSettle.restart()
    onSortDescendingChanged: optionsSettle.restart()
    onFiltersChanged: optionsSettle.restart()
    onDirsFirstChanged: optionsSettle.restart()
    onHiddenLastChanged: optionsSettle.restart()

    Timer {
        id: optionsSettle

        interval: 0
        onTriggered: pool.repageAll()
    }

    DirectoryPool {
        id: pool

        backend: root.backend
        activePath: root.unlistedPaths.includes(root.path) ? "" : root.path
        includeHidden: root.showHidden
        filters: root.filters
        sortKey: root.sortKey
        sortDesc: root.sortDescending
        dirsFirst: root.dirsFirst
        hiddenLast: root.hiddenLast
        nameFilter: root.nameFilter
        thumbnailsEnabled: root.thumbnailsEnabled
        thumbnailSizeLimit: root.thumbnailSizeLimit
        networkThumbnails: root.networkThumbnails
    }

    Connections {
        target: root.directory

        function onGone() {
            history.up();
        }

        function onListed() {
            root._applyPendingReveal();
        }

        function onEntriesAdded() {
            root._applyPendingReveal();
        }
    }

    SelectionModel {
        id: selectionModel
    }

    NavigationHistory {
        id: history

        onEntering: target => root.navigating(target)
        onRestored: saved => {
            if (!saved)
                return;
            if (saved.cursor)
                selectionModel.select(saved.cursor);
            viewBody.restoreScroll(saved.contentY ?? 0);
        }
    }

    FileBrowserSidebar {
        id: sidebar

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: root.sidebarShown ? FileBrowserMetrics.sidebarWidth : 0
        visible: root.sidebarShown
        places: root.backend?.userDirs ?? []
        currentPath: root.path
        onPlaceSelected: target => {
            root.focusRequested();
            root.navigate(target);
        }
    }

    Item {
        id: main

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: sidebar.right
        anchors.leftMargin: root.sidebarShown ? FileBrowserMetrics.contentSpacing : 0
        anchors.right: parent.right

        Item {
            id: topbar

            readonly property bool shown: root.showNavigation || toolbarExtras.children.length > 0

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: shown ? FileBrowserMetrics.controlSize : 0
            visible: shown

            NavButtonPair {
                id: navPair

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                visible: root.showNavigation
                backEnabled: root.canGoBack
                forwardEnabled: root.canGoForward
                onBackRequested: root.back()
                onForwardRequested: root.forward()
            }

            Row {
                id: toolbarExtras

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: FileBrowserMetrics.controlSpacing
                LayoutMirroring.enabled: I18n.isRtl
                LayoutMirroring.childrenInherit: true
            }
        }

        Rectangle {
            id: pane

            anchors.top: topbar.bottom
            anchors.topMargin: topbar.shown ? FileBrowserMetrics.contentSpacing : 0
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            radius: root.paneRadius
            color: root.paneColor
            border.width: root.paneBorderWidth
            border.color: root.paneBorderColor

            Item {
                id: paneBackground

                anchors.fill: parent
                anchors.rightMargin: drawer.width

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: point => {
                        root.focusRequested();
                        const pos = paneBackground.mapToItem(root, point.position.x, point.position.y);
                        root.backgroundMenuRequested(pos.x, pos.y);
                    }
                }
            }

            FileDropArea {
                anchors.fill: parent
                enabled: root.dropEnabled && root.directory.path !== ""
                dropPath: root.directory.path
                scrollView: viewBody.view?.activeList ?? viewBody.view
                onDropped: drop => root.itemsDropped(root.directory.path, drop)
            }

            PathBar {
                id: pathBar

                visible: root.showPathBar
                height: visible ? implicitHeight : 0
                readonly property bool trailingShown: pathBarTrailingLoader.visible && pathBarTrailingLoader.width > 0

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: trailingShown ? pathBarTrailingLoader.left : parent.right
                anchors.topMargin: root.paneMargin
                anchors.leftMargin: root.paneMarginHorizontal
                anchors.rightMargin: trailingShown ? FileBrowserMetrics.contentSpacing : root.paneMarginHorizontal
                path: root.path
                homePath: root.homePath
                chipColor: root.chipColor
                folderJump: root.folderJump
                backend: root.backend
                dropEnabled: root.dropEnabled
                onEditingFinished: root.focusBody()
                onTabRequested: target => root.tabRequested(target)
                onItemsDropped: (dest, drop) => root.itemsDropped(dest, drop)
                onMenuRequested: (target, pointX, pointY) => {
                    root.focusRequested();
                    const point = pathBar.mapToItem(root, pointX, pointY);
                    root.pathMenuRequested(target, point.x, point.y);
                }
                onNavigated: target => {
                    root.focusRequested();
                    root.navigate(target);
                }
                onRevealRequested: targets => {
                    root.focusRequested();
                    root.reveal(targets);
                }
                onLocationRequested: location => root.locationRequested(location)
                onLocationCancelled: root.locationCancelled()
            }

            Loader {
                id: pathBarTrailingLoader

                anchors.right: parent.right
                anchors.rightMargin: root.paneMarginHorizontal
                anchors.verticalCenter: pathBar.verticalCenter
                active: root.pathBarTrailing !== null
                visible: !root.editingPath
                sourceComponent: root.pathBarTrailing
            }

            FileViewBody {
                id: viewBody

                anchors.top: pathBar.bottom
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: root.paneMargin
                anchors.bottomMargin: root.paneMargin
                anchors.leftMargin: root.paneMarginHorizontal
                anchors.rightMargin: root.paneMarginHorizontal + drawer.width
                focus: true
                directory: root.directory
                directoryPool: pool
                selection: selectionModel
                viewMode: root.viewMode
                gridZoom: root.gridZoom
                listZoom: root.listZoom
                columnZoom: root.columnZoom
                columnPaneWidth: root.columnPaneWidth
                listColumns: root.listColumns
                columnWidths: root.columnWidths
                sortKey: root.sortKey
                sortDescending: root.sortDescending
                showHidden: root.showHidden
                multiSelect: root.multiSelect
                dragEnabled: root.dragEnabled
                dropEnabled: root.dropEnabled
                showSelectionFooter: root.showSelectionFooter
                previewOpen: root.previewOpen
                previewKeys: root.previewKeys
                escapeClearsSelection: root.escapeClearsSelection
                emptyText: root.emptyText
                surfaceColor: root.paneColor
                chipColor: root.chipColor
                header: root.header
                cutSet: root.cutSet
                singleClickActivates: root.singleClickActivates
                typeAheadEnabled: root.typeAheadEnabled
                typeAheadMode: root.typeAheadMode
                countFolders: root.countFolders
                homePath: root.homePath
                nameHighlight: NameFilter.needle(root.nameFilter)
                filtering: root.nameFilter !== ""
                onFilterRequested: text => root.filterRequested(text)
                onFilterFocusRequested: root.filterFocusRequested()
                onActivateRequested: entry => root.activateRequested(entry)
                onItemMenuRequested: (index, pointX, pointY, modifiers) => {
                    root.focusRequested();
                    const point = mapToItem(root, pointX, pointY);
                    root.itemMenuRequested(index, point.x, point.y, modifiers);
                }
                onBackgroundMenuRequested: (pointX, pointY) => {
                    root.focusRequested();
                    const point = mapToItem(root, pointX, pointY);
                    root.backgroundMenuRequested(point.x, point.y);
                }
                onColumnsMenuRequested: (pointX, pointY) => {
                    const point = mapToItem(root, pointX, pointY);
                    root.columnsMenuRequested(point.x, point.y);
                }
                onItemsDropped: (dest, drop) => root.itemsDropped(dest, drop)
                onDragRequested: paths => root.dragRequested(paths)
                onRenameRequested: (target, name) => root.renameRequested(target, name)
                onSortRequested: key => root.sortRequested(key)
                onZoomRequested: (mode, level) => root.zoomRequested(mode, level)
                onColumnResizeRequested: (id, width) => root.columnResizeRequested(id, width)
                onColumnPaneResizeRequested: width => root.columnPaneResizeRequested(width)
                onDirectoryReplaced: {
                    if (!root.directory.loading)
                        root._applyPendingReveal();
                }
                onPreviewKeyRequested: (action, step) => {
                    if (action === "toggle") {
                        root.previewToggleRequested();
                        return;
                    }
                    drawer.handleKey(action, step);
                }
                onFocusRequested: {
                    root._pendingReveal = [];
                    root._pendingRename = "";
                    root.focusRequested();
                }
                onTabRequested: target => root.tabRequested(target)
                onRevealRequested: target => {
                    root.focusRequested();
                    root.reveal(target);
                }
                onOpenRequested: target => {
                    root.focusRequested();
                    if (target === "..") {
                        root.up();
                        return;
                    }
                    root.navigate(target);
                }
            }

            FilePreviewDrawer {
                id: drawer

                anchors.top: viewBody.top
                anchors.bottom: viewBody.bottom
                anchors.right: parent.right
                anchors.rightMargin: root.paneMarginHorizontal
                body: viewBody
                visible: root.previewDrawer
                open: root.previewDrawer && root.previewOpen && !viewBody.columnsMode
                available: pane.width - root.paneMarginHorizontal * 2
                preferredWidth: root.previewWidth
                chipColor: root.chipColor
                onResized: width => root.previewResized(width)
                onFocusRequested: root.focusRequested()
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        onTapped: (point, button) => {
            root.focusRequested();
            if (button === Qt.BackButton) {
                root.back();
                return;
            }
            root.forward();
        }
    }
}
