pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

FocusScope {
    id: root

    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    property string mode: "open"
    property var filters: []
    property bool multiple: false
    property string defaultName: ""
    property string startPath: ""
    property string bucket: "default"
    property string title: I18n.tr("Select File", "default file browser window title")
    property bool showHeader: true
    property var windowControls: null
    property bool defaultShowHidden: false
    property string defaultViewMode: "list"
    property var backend: Host.files
    property bool autoReset: true

    property string viewMode: defaultViewMode
    property string sortKey: "name"
    property bool sortDescending: false
    property int gridZoom: FileBrowserMetrics.defaultGridZoom
    property int listZoom: FileBrowserMetrics.defaultListZoom
    property bool showSidebar: true
    property bool showHidden: defaultShowHidden

    readonly property bool saving: mode === "save"
    readonly property string currentPath: browser.path
    readonly property alias browser: browser
    readonly property alias sortControl: sortButton
    property alias fileName: nameField.text
    readonly property var capabilities: backend?.capabilities ?? ({})
    readonly property string typedTarget: {
        const typed = nameField.text.trim();
        if (/^(\/|[A-Za-z]:)/.test(typed) || typed.startsWith("~"))
            return FilePaths.normalize(typed);
        return typed === "" || browser.path === "" ? "" : FilePaths.join(browser.path, typed);
    }
    readonly property bool canAccept: {
        switch (mode) {
        case "save":
            return typedTarget !== "" && FileFormat.validName(FilePaths.baseName(typedTarget));
        case "openFolder":
            return browser.path !== "";
        default:
            return browser.selectedEntries.length > 0;
        }
    }

    property string _error: ""
    property string _overwriteTarget: ""
    property string _rememberedPath: ""
    property bool _restoring: false

    signal accepted(var paths)
    signal rejected

    function reset() {
        const saved = FileBrowserSettings.load(bucket);
        _restoring = true;
        viewMode = saved.viewMode || defaultViewMode;
        sortKey = saved.sortKey;
        sortDescending = saved.sortDesc;
        gridZoom = saved.gridZoom;
        listZoom = saved.listZoom;
        showSidebar = saved.showSidebar;
        showHidden = saved.showHidden ?? defaultShowHidden;
        _error = "";
        _overwriteTarget = "";
        _rememberedPath = "";
        nameField.text = defaultName;
        browser.restoreState({
            "path": ""
        });
        _restoring = false;
        _openStart(saved.lastPath);
        _focusStart();
    }

    function setViewMode(value) {
        viewMode = value;
        _persist({
            "viewMode": value
        });
    }

    function setSort(key, descending) {
        sortKey = key;
        sortDescending = descending;
        _persist({
            "sortKey": key,
            "sortDesc": descending
        });
    }

    function setZoom(forMode, level) {
        if (forMode === "grid") {
            gridZoom = level;
            _persist({
                "gridZoom": level
            });
            return;
        }
        listZoom = level;
        _persist({
            "listZoom": level
        });
    }

    function setShowSidebar(value) {
        showSidebar = value;
        _persist({
            "showSidebar": value
        });
    }

    function setShowHidden(value) {
        showHidden = value;
        _persist({
            "showHidden": value
        });
    }

    function activate(entry) {
        _error = "";
        if (entry.isDir) {
            browser.navigate(entry.path);
            return;
        }
        switch (mode) {
        case "openFolder":
            return;
        case "save":
            nameField.text = entry.name;
            _overwriteTarget = entry.path;
            return;
        default:
            _finish([entry.path]);
        }
    }

    function acceptCurrent() {
        if (!canAccept)
            return;
        _error = "";
        const selected = browser.selectedEntries;
        switch (mode) {
        case "save":
            _save(typedTarget);
            return;
        case "openFolder":
            {
                const dirs = selected.filter(entry => entry.isDir);
                _finish([dirs.length === 1 ? dirs[0].path : browser.path]);
                return;
            }
        default:
            {
                if (selected.length === 1 && selected[0].isDir) {
                    browser.navigate(selected[0].path);
                    return;
                }
                const files = selected.filter(entry => !entry.isDir).map(entry => entry.path);
                if (files.length === 0)
                    return;
                _finish(multiple ? files : [files[0]]);
            }
        }
    }

    function createFolder(name) {
        const target = FilePaths.join(browser.path, name.trim());
        newFolderDialog.opened = false;
        _focusStart();
        backend.mkdir(target, result => {
            if (result.error) {
                root._error = FileFormat.operationError(result.code, result.error);
                return;
            }
            browser.navigate(result.path || target);
        });
    }

    function renameItem(target, name) {
        backend.rename(target, name, result => {
            if (!result.error)
                return;
            root._error = FileFormat.operationError(result.code, result.error);
        });
    }

    function trashItems(paths) {
        backend.trash(paths, result => {
            const failed = result.failed ?? [];
            if (result.error || failed.length > 0)
                root._error = FileFormat.operationError(result.code ?? failed[0]?.code ?? "", result.error ?? failed[0]?.error ?? "");
        });
    }

    function _save(target) {
        if (!backend?.connected) {
            _finish([target]);
            return;
        }
        backend.stat(target, result => {
            if (result.error) {
                if (result.code === "ENOENT") {
                    root._finish([target]);
                    return;
                }
                root._error = FileFormat.operationError(result.code, result.error);
                return;
            }
            if (result.entry?.isDir) {
                nameField.text = "";
                browser.navigate(target);
                return;
            }
            root._overwriteTarget = target;
        });
    }

    function _finish(paths) {
        _rememberPath();
        accepted(paths);
    }

    function _rememberPath() {
        if (browser.path === "" || browser.path === _rememberedPath)
            return;
        _rememberedPath = browser.path;
        _persist({
            "lastPath": browser.path
        });
    }

    function _persist(patch) {
        if (_restoring)
            return;
        FileBrowserSettings.save(bucket, patch);
    }

    function _openStart(lastPath) {
        const fallback = lastPath || browser.homePath;
        const start = FilePaths.normalize(startPath);
        if (start === "") {
            browser.navigate(fallback);
            return;
        }
        if (!backend?.connected) {
            browser.reveal(start);
            return;
        }
        backend.stat(start, result => {
            if (result.error) {
                browser.navigate(fallback);
                return;
            }
            if (result.entry?.isDir) {
                browser.navigate(start);
                return;
            }
            browser.reveal(start);
        });
    }

    function _focusStart() {
        if (saving) {
            nameField.forceActiveFocus();
            nameField.selectAll();
            return;
        }
        browser.focusBody();
    }

    Keys.onPressed: event => {
        const control = (event.modifiers & Qt.ControlModifier) !== 0;
        const alt = (event.modifiers & Qt.AltModifier) !== 0;
        switch (event.key) {
        case Qt.Key_Escape:
            root.rejected();
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            {
                if (!browser.body.activeFocus)
                    return;
                const entry = browser.currentEntry;
                if (entry && browser.selection.count === 1 && browser.selection.contains(entry.path)) {
                    root.activate(entry);
                    break;
                }
                root.acceptCurrent();
                break;
            }
        case Qt.Key_A:
            if (!control || !root.multiple)
                return;
            browser.selection.selectAll(browser.directory.entries);
            break;
        case Qt.Key_L:
            if (!control)
                return;
            browser.editPath();
            break;
        case Qt.Key_H:
            if (!control)
                return;
            root.setShowHidden(!root.showHidden);
            break;
        case Qt.Key_Left:
            if (!alt)
                return;
            browser.back();
            break;
        case Qt.Key_Right:
            if (!alt)
                return;
            browser.forward();
            break;
        case Qt.Key_Up:
            if (!alt)
                return;
            browser.up();
            break;
        case Qt.Key_F2:
            {
                const entry = browser.currentEntry;
                if (!entry || root.capabilities.rename !== true)
                    return;
                browser.beginRename(entry.path);
                break;
            }
        default:
            return;
        }
        event.accepted = true;
    }

    Component.onCompleted: {
        if (autoReset)
            reset();
    }
    Component.onDestruction: _rememberPath()
    onRejected: _rememberPath()

    Connections {
        target: browser

        function onPathChanged() {
            root._error = "";
        }
    }

    Connections {
        target: browser.selection
        enabled: root.saving

        function onPathsChanged() {
            const selected = browser.selectedEntries;
            if (selected.length !== 1 || selected[0].isDir)
                return;
            nameField.text = selected[0].name;
        }
    }

    DWindowHeader {
        id: header

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: root.showHeader
        height: !visible ? 0 : collapsed ? implicitHeight : Math.max(implicitHeight, FileBrowserMetrics.headerHeight)
        controls: root.windowControls
        title: root.title
        onCloseRequested: root.rejected()
    }

    component ToolbarButton: DIconButton {
        width: FileBrowserMetrics.controlSize
        buttonSize: FileBrowserMetrics.controlSize
        iconSize: Style.iconSizeMedium
        variant: "tonal"
        containerColor: checked ? Style.primary : Style.chipSurface
        contentColor: checked ? Style.onPrimary : Style.surfaceText
    }

    FileBrowserView {
        id: browser

        anchors.top: header.bottom
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: FileBrowserMetrics.contentMargin
        anchors.bottomMargin: FileBrowserMetrics.contentSpacing
        focus: !root.saving
        backend: root.backend
        showSidebar: root.showSidebar
        sidebarBreakpoint: Style.smallBreakpoint + FileBrowserMetrics.sidebarWidth
        showHidden: root.showHidden
        filters: root.filters
        sortKey: root.sortKey
        sortDescending: root.sortDescending
        viewMode: root.viewMode
        gridZoom: root.gridZoom
        listZoom: root.listZoom
        listColumns: ["size", "modified"]
        multiSelect: root.multiple
        escapeClearsSelection: false
        paneColor: Style.isFloatingWindow(root) ? Style.floatingWindowNestedSurface : Style.nestedSurface
        chipColor: Style.chipSurfaceNested
        onActivateRequested: entry => root.activate(entry)
        onFocusRequested: browser.focusBody()
        onSortRequested: key => root.setSort(key, key === root.sortKey ? !root.sortDescending : false)
        onZoomRequested: (forMode, level) => root.setZoom(forMode, level)
        onRenameRequested: (target, name) => root.renameItem(target, name)
        onItemMenuRequested: (index, pointX, pointY) => {
            const entry = browser.body.entryAt(index);
            if (!entry)
                return;
            itemMenu.entry = entry;
            const point = browser.mapToItem(root, pointX, pointY);
            itemMenu.openAt(point.x, point.y);
        }
        onBackgroundMenuRequested: (pointX, pointY) => {
            const point = browser.mapToItem(root, pointX, pointY);
            backgroundMenu.openAt(point.x, point.y);
        }

        ToolbarButton {
            iconName: "menu"
            tooltipText: I18n.tr("Quick Access", "file browser sidebar section header")
            onClicked: root.setShowSidebar(!root.showSidebar)
        }

        ToolbarButton {
            visible: root.capabilities.mkdir === true
            iconName: "create_new_folder"
            tooltipText: I18n.tr("New folder", "file browser toolbar button creating a folder")
            onClicked: newFolderDialog.show()
        }

        ToolbarButton {
            checkable: true
            checked: root.showHidden
            iconName: root.showHidden ? "visibility" : "visibility_off"
            tooltipText: I18n.tr("Show hidden files", "file browser toolbar toggle")
            onClicked: root.setShowHidden(!root.showHidden)
        }

        ToolbarButton {
            iconName: root.viewMode === "grid" ? "view_list" : "grid_view"
            tooltipText: root.viewMode === "grid" ? I18n.tr("List view", "file browser toolbar button switching to the list") : I18n.tr("Grid view", "file browser toolbar button switching to the grid")
            onClicked: root.setViewMode(root.viewMode === "grid" ? "list" : "grid")
        }

        FileSortButton {
            id: sortButton

            sortKey: root.sortKey
            descending: root.sortDescending
            onSortKeySelected: key => root.setSort(key, root.sortDescending)
            onDirectionToggled: root.setSort(root.sortKey, !root.sortDescending)
        }
    }

    Item {
        id: footer

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: FileBrowserMetrics.contentMargin
        height: Style.buttonHeightS

        DTextField {
            id: nameField

            anchors.left: parent.left
            anchors.right: buttons.left
            anchors.rightMargin: Style.spacingM
            anchors.verticalCenter: parent.verticalCenter
            visible: root.saving
            controlHeight: Style.buttonHeightS
            placeholderText: I18n.tr("Enter filename...", "file browser save filename input placeholder")
            isError: text.trim() !== "" && !root.canAccept
            onAccepted: root.acceptCurrent()
            Keys.onReturnPressed: event => event.accepted = true
            Keys.onEnterPressed: event => event.accepted = true
            Keys.onEscapePressed: event => {
                event.accepted = true;
                root.rejected();
            }
        }

        StyledText {
            anchors.left: parent.left
            anchors.right: buttons.left
            anchors.rightMargin: Style.spacingM
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.saving
            text: root._error
            color: Style.error
            font.pixelSize: Style.fontSizeSmall
            elide: Text.ElideRight
        }

        Row {
            id: buttons

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Style.spacingS
            LayoutMirroring.enabled: I18n.isRtl

            DButton {
                text: I18n.tr("Cancel", "file browser overwrite dialog cancel button")
                variant: "text"
                onClicked: root.rejected()
            }

            DButton {
                enabled: root.canAccept
                text: {
                    switch (root.mode) {
                    case "save":
                        return I18n.tr("Save", "file browser save button");
                    case "openFolder":
                        return I18n.tr("Use this folder", "file browser folder selection confirm button");
                    default:
                        return I18n.tr("Open", "file browser open button");
                    }
                }
                onClicked: root.acceptCurrent()
            }
        }
    }

    StyledText {
        anchors.bottom: footer.top
        anchors.left: footer.left
        anchors.right: footer.right
        anchors.bottomMargin: Style.spacingXS
        visible: root.saving && root._error !== ""
        text: root._error
        color: Style.error
        font.pixelSize: Style.fontSizeSmall
        elide: Text.ElideRight
    }

    FileMenu {
        id: itemMenu

        property var entry: null

        anchorItem: root

        FileMenuItem {
            visible: itemMenu.entry?.isDir === true
            iconName: "folder_open"
            text: I18n.tr("Open", "file browser open button")
            onClicked: {
                itemMenu.close();
                browser.navigate(itemMenu.entry.path);
            }
        }

        FileMenuItem {
            visible: root.capabilities.rename === true
            iconName: "edit"
            text: I18n.tr("Rename", "file browser item context menu action")
            onClicked: {
                itemMenu.close();
                browser.beginRename(itemMenu.entry.path);
            }
        }

        FileMenuItem {
            visible: root.capabilities.trash === true
            iconName: "delete"
            text: I18n.tr("Move to Trash", "file browser item context menu action")
            onClicked: {
                itemMenu.close();
                const target = itemMenu.entry.path;
                root.trashItems(browser.selection.contains(target) ? browser.selection.paths.slice() : [target]);
            }
        }

        FileMenuItem {
            iconName: "content_copy"
            text: I18n.tr("Copy path", "file browser item context menu action")
            onClicked: {
                itemMenu.close();
                Paths.copyPathToClipboard(itemMenu.entry.path);
            }
        }
    }

    FileMenu {
        id: backgroundMenu

        anchorItem: root

        FileMenuItem {
            visible: root.capabilities.mkdir === true
            iconName: "create_new_folder"
            text: I18n.tr("New folder", "file browser toolbar button creating a folder")
            onClicked: {
                backgroundMenu.close();
                newFolderDialog.show();
            }
        }

        FileMenuItem {
            iconName: "content_copy"
            text: I18n.tr("Copy path", "file browser item context menu action")
            onClicked: {
                backgroundMenu.close();
                Paths.copyPathToClipboard(browser.path);
            }
        }
    }

    DDialog {
        id: newFolderDialog

        function show() {
            folderName.text = "";
            opened = true;
            folderName.forceActiveFocus();
        }

        anchors.fill: parent
        embedded: false
        opened: false
        title: I18n.tr("New folder", "file browser toolbar button creating a folder")
        acceptEnabled: FileFormat.validName(folderName.text)
        onAccepted: root.createFolder(folderName.text)
        onRejected: {
            opened = false;
            root._focusStart();
        }

        DTextField {
            id: folderName

            width: parent.width
            outlined: true
            labelText: I18n.tr("Folder name", "label of the name field in the new folder dialog")
            onAccepted: {
                if (newFolderDialog.acceptEnabled)
                    root.createFolder(text);
            }
        }

        actions: [
            DButton {
                text: I18n.tr("Cancel", "file browser overwrite dialog cancel button")
                variant: "text"
                onClicked: newFolderDialog.rejected()
            },
            DButton {
                enabled: newFolderDialog.acceptEnabled
                text: I18n.tr("Create", "confirm button of the new folder dialog")
                onClicked: newFolderDialog.accepted()
            }
        ]
    }

    DDialog {
        id: overwriteDialog

        anchors.fill: parent
        embedded: false
        opened: root._overwriteTarget !== ""
        title: I18n.tr("File Already Exists", "file browser overwrite dialog title")
        supportingText: I18n.tr("A file with this name already exists. Do you want to overwrite it?", "file browser overwrite dialog message")
        onAccepted: {
            const target = root._overwriteTarget;
            root._overwriteTarget = "";
            root._finish([target]);
        }
        onRejected: {
            root._overwriteTarget = "";
            root._focusStart();
        }
        onOpenedChanged: {
            if (opened)
                forceActiveFocus();
        }

        actions: [
            DButton {
                text: I18n.tr("Cancel", "file browser overwrite dialog cancel button")
                variant: "text"
                onClicked: overwriteDialog.rejected()
            },
            DButton {
                text: I18n.tr("Overwrite", "file browser overwrite dialog confirm button")
                variant: "tonal"
                danger: true
                onClicked: overwriteDialog.accepted()
            }
        ]
    }
}
