pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets
import "PathCompletion.js" as PathCompletion

FocusScope {
    id: bar

    property string path: ""
    property string homePath: FilePaths.home
    property color chipColor: Style.chipSurface
    property bool editing: false
    property bool editable: true
    property bool flat: false
    signal editRequested
    property bool jumpMode: false
    property bool connecting: false
    property var backend: null
    property var folderJump: null
    property bool dropEnabled: false
    property string errorText: ""

    property var completions: []
    property string completionKey: ""
    property int completionRequest: 0
    property int resolveRequest: 0
    property real lastPillClick: 0

    readonly property bool canComplete: typeof backend?.list === "function"
    readonly property bool canResolve: typeof backend?.resolveLocation === "function"
    readonly property string typed: field.text.trim()
    readonly property var completionTarget: editing && !connecting && canComplete ? PathCompletion.target(typed, path, homePath) : null
    readonly property string completionDir: completionTarget?.dir ?? ""
    readonly property string completionPrefix: completionTarget?.prefix ?? ""
    readonly property var rows: {
        if (!editing || connecting)
            return [];
        const completed = completionTarget ? PathCompletion.rank(completions, completionTarget, homePath, FileBrowserMetrics.suggestionCap) : [];
        const visited = visitedRows(typed);
        return jumpMode ? PathCompletion.merge(visited, completed) : PathCompletion.merge(completed, visited);
    }

    signal navigated(string target)
    signal revealRequested(var targets)
    signal locationRequested(var location)
    signal locationCancelled
    signal editingFinished
    signal tabRequested(string target)
    signal menuRequested(string target, real pointX, real pointY)
    signal itemsDropped(string dest, var drop)

    readonly property var segments: bar.segmentsFor(path, homePath)

    function segmentsFor(path, home) {
        if (path === "")
            return [];

        const rooted = home !== "" && (path === home || path.startsWith(home + "/"));
        const head = rooted ? {
            "label": "",
            "iconName": "home",
            "path": home
        } : {
            "label": "",
            "iconName": "hard_drive",
            "path": "/"
        };

        const rest = rooted ? path.substring(home.length) : path;
        const out = [head];
        let walked = head.path === "/" ? "" : head.path;
        for (const name of rest.split("/")) {
            if (name === "")
                continue;
            walked = walked + "/" + name;
            out.push({
                "label": name,
                "iconName": "",
                "path": walked
            });
        }
        return out;
    }

    function startEditing() {
        if (!editable) {
            editRequested();
            return;
        }
        beginEditing(false, path);
        field.selectAll();
    }

    function startJump() {
        beginEditing(true, "");
    }

    function beginEditing(jump, text) {
        if (connecting)
            return;
        jumpMode = jump;
        editing = true;
        errorText = "";
        field.text = text;
        suggestionPopup.highlightIndex = -1;
        field.forceActiveFocus();
        fetchCompletions();
    }

    function fetchCompletions() {
        if (!editing || !canComplete || completionDir === "") {
            completions = [];
            completionKey = "";
            return;
        }
        const hidden = completionPrefix.startsWith(".");
        const key = completionDir + (hidden ? "\u0000." : "");
        if (key === completionKey)
            return;
        completionKey = key;
        completions = [];
        const request = ++completionRequest;
        backend.list(completionDir, {
            "limit": FileBrowserMetrics.completionLimit,
            "includeHidden": hidden,
            "dirsFirst": true,
            "hiddenLast": true,
            "sortKey": "name"
        }, result => {
            if (request !== bar.completionRequest)
                return;
            bar.completions = result.error ? [] : (result.entries || []).filter(entry => entry.isDir);
        });
    }

    function completeTab() {
        const next = PathCompletion.tab(field.text, rows, suggestionPopup.highlightIndex);
        if (!next)
            return false;
        if (next.text !== field.text) {
            field.text = next.text;
            field.cursorPosition = next.text.length;
        }
        suggestionPopup.highlightIndex = next.highlight;
        return true;
    }

    function reset() {
        editing = false;
        jumpMode = false;
        errorText = "";
        resolveRequest++;
        suggestionPopup.highlightIndex = -1;
        completions = [];
        completionKey = "";
        field.text = "";
    }

    function stopEditing() {
        reset();
        editingFinished();
    }

    function cancelConnecting() {
        connecting = false;
        stopEditing();
        locationCancelled();
    }

    function settleLocation(target) {
        if (!connecting)
            return;
        connecting = false;
        stopEditing();
        if (target !== "")
            navigated(target);
    }

    function visitedRows(text) {
        if (!folderJump || PathCompletion.isPathLike(text) || (text === "" && !jumpMode))
            return [];
        const matches = text === "" ? folderJump.top(FileBrowserMetrics.suggestionCap, path) : folderJump.match(text, FileBrowserMetrics.suggestionCap, path);
        return matches.map(match => PathCompletion.row(match, "visited"));
    }

    function verifyShown() {
        const shown = rows.slice(0, FileBrowserMetrics.suggestionRows).filter(row => row.kind === "visited");
        if (shown.length > 0 && typeof folderJump?.verify === "function")
            folderJump.verify(shown.map(row => row.path));
    }

    function describeError(code) {
        switch (code) {
        case "ENOENT":
            return I18n.tr("Nothing is at this location", "location bar error when the typed location does not exist");
        case "EACCES":
            return I18n.tr("You do not have permission to view this folder", "directory listing error");
        case "EOTHERHOME":
            return I18n.tr("Use ~ for your own home folder or type the full path", "location bar error when another user's home was typed as ~name");
        case "EINVAL":
            return I18n.tr("Type a path, ~ or an address such as sftp://host", "location bar error when the text is not a location");
        case "NOTSUPPORTED":
            return I18n.tr("This kind of location is not supported", "location bar error for an unknown address scheme");
        }
        return I18n.tr("This folder could not be opened", "directory listing error");
    }

    function apply(location) {
        switch (location.kind) {
        case "folder":
            stopEditing();
            if (location.select.length > 0) {
                revealRequested(location.select);
                return;
            }
            navigated(location.path);
            return;
        case "remote":
            errorText = "";
            connecting = true;
            field.text = location.uri;
            locationRequested(location);
            return;
        }
        stopEditing();
        locationRequested(location);
    }

    function resolveLocally(text, fallback) {
        const expanded = FilePaths.expandTilde(text);
        const target = /^(\/|[A-Za-z]:)/.test(expanded) ? expanded : (fallback?.path ?? "");
        stopEditing();
        if (target !== "")
            navigated(target);
    }

    function resolve(text, fallback, staleJump) {
        if (!canResolve) {
            resolveLocally(text, fallback);
            return;
        }
        const request = ++resolveRequest;
        backend.resolveLocation(text, path, result => {
            if (request !== bar.resolveRequest || !bar.editing)
                return;
            if (!result.error) {
                bar.apply(result.location);
                return;
            }
            if (result.code === "ENOENT" && fallback) {
                bar.openRow(fallback);
                return;
            }
            if (result.code === "ENOENT" && staleJump !== "")
                bar.folderJump?.forget(staleJump);
            bar.errorText = bar.describeError(result.code);
        });
    }

    function pillClicked(target) {
        const now = Date.now();
        if (now - lastPillClick < Qt.styleHints.mouseDoubleClickInterval) {
            lastPillClick = 0;
            startEditing();
            return;
        }
        lastPillClick = now;
        navigated(target);
    }

    function openRow(row) {
        resolve(row.path, null, row.kind === "visited" ? row.path : "");
    }

    function acceptTyped() {
        if (connecting)
            return;
        const picked = suggestionPopup.highlightedRow();
        if (picked) {
            openRow(picked);
            return;
        }
        if (typed === "") {
            stopEditing();
            return;
        }
        resolve(typed, rows.length > 0 && !PathCompletion.isPathLike(typed) ? rows[0] : null, "");
    }

    onPathChanged: {
        if (connecting) {
            cancelConnecting();
            return;
        }
        if (editing)
            stopEditing();
    }
    onCompletionDirChanged: fetchCompletions()
    onCompletionPrefixChanged: {
        if (completionPrefix.startsWith(".") !== completionKey.endsWith("."))
            fetchCompletions();
    }
    onRowsChanged: verifyShown()

    implicitHeight: FileBrowserMetrics.pathPillHeight

    Keys.onEscapePressed: event => {
        if (connecting) {
            cancelConnecting();
            return;
        }
        if (!editing) {
            event.accepted = false;
            return;
        }
        stopEditing();
    }

    Keys.onPressed: event => {
        if (!editing || connecting || rows.length === 0)
            return;
        switch (event.key) {
        case Qt.Key_Down:
            suggestionPopup.move(1);
            break;
        case Qt.Key_Up:
            suggestionPopup.move(-1);
            break;
        case Qt.Key_PageDown:
            suggestionPopup.page(1);
            break;
        case Qt.Key_PageUp:
            suggestionPopup.page(-1);
            break;
        case Qt.Key_Tab:
            if (!completeTab())
                return;
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    PathSuggestions {
        id: suggestionPopup

        anchorItem: field
        rows: bar.rows
        errorText: bar.errorText
        visible: bar.editing && !bar.connecting && (bar.rows.length > 0 || bar.errorText !== "")
        onChosen: row => bar.openRow(row)
    }

    Rectangle {
        id: frame

        anchors.fill: parent
        visible: !bar.editing && !bar.connecting
        radius: FileBrowserMetrics.pathPillRadius
        color: Style.foregroundColor(bar.chipColor, Style.isFloatingWindow(bar))

        Rectangle {
            anchors.fill: parent
            radius: frame.radius
            color: Style.surfaceText
            opacity: stripArea.containsMouse ? Style.stateLayerHover : 0
        }

        DFlickable {
            id: strip

            readonly property bool mirrored: LayoutMirroring.enabled
            readonly property real inset: Style.spacingXXS
            readonly property real overflow: Math.max(0, contentWidth - width)

            function scrollToCurrent() {
                contentX = mirrored ? 0 : overflow;
            }

            anchors.fill: parent
            wheelEnabled: false
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            contentWidth: Math.max(width, pills.width + inset * 2)
            contentHeight: height

            onContentWidthChanged: scrollToCurrent()
            onWidthChanged: scrollToCurrent()

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => {
                    const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x;
                    strip.contentX = Math.max(0, Math.min(strip.contentWidth - strip.width, strip.contentX - delta));
                }
            }

            MouseArea {
                id: stripArea

                width: strip.contentWidth
                height: strip.height
                hoverEnabled: true
                cursorShape: Qt.IBeamCursor
                onClicked: bar.startEditing()
            }

            Row {
                id: pills

                x: strip.mirrored ? strip.contentWidth - width - strip.inset : strip.inset
                y: strip.inset
                height: strip.height - strip.inset * 2
                spacing: FileBrowserMetrics.pathBarSpacing

                Repeater {
                    model: bar.segments

                    Row {
                        id: segment

                        required property int index
                        required property var modelData

                        anchors.verticalCenter: pills.verticalCenter
                        spacing: FileBrowserMetrics.pathBarSpacing

                        StyledText {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: segment.index > 0
                            text: "/"
                            color: Style.outline
                            font.pixelSize: Style.fontSizeMedium
                        }

                        PathPill {
                            id: pill

                            height: pills.height
                            radius: Math.max(0, FileBrowserMetrics.pathPillRadius - strip.inset)
                            label: segment.modelData.iconName === "home" ? I18n.tr("Home", "file browser quick access location") : segment.modelData.label
                            iconName: segment.modelData.iconName
                            path: segment.modelData.path
                            flat: bar.flat
                            chipColor: "transparent"
                            current: segment.index === bar.segments.length - 1
                            dropEnabled: bar.dropEnabled
                            onClicked: bar.pillClicked(segment.modelData.path)
                            onMiddleClicked: bar.tabRequested(segment.modelData.path)
                            onDropped: drop => bar.itemsDropped(segment.modelData.path, drop)
                            onSpringLoaded: bar.navigated(segment.modelData.path)
                            onMenuRequested: (pointX, pointY) => {
                                const point = pill.mapToItem(bar, pointX, pointY);
                                bar.menuRequested(segment.modelData.path, point.x, point.y);
                            }
                        }
                    }
                }
            }
        }
    }

    DTextField {
        id: field

        anchors.fill: parent
        visible: bar.editing || bar.connecting
        readOnly: bar.connecting
        cornerRadius: FileBrowserMetrics.pathPillRadius
        controlHeight: bar.height
        backgroundColor: bar.chipColor
        leftIconName: bar.jumpMode ? "history" : "edit_location"
        leftIconSize: FileBrowserMetrics.pathIconSize
        leadingContent: bar.connecting ? connectingIndicator : null
        placeholderText: bar.jumpMode ? I18n.tr("Jump to a folder you visited", "location bar placeholder in folder jump mode") : ""
        hidePlaceholderOnFocus: false
        isError: bar.errorText !== ""
        font.pixelSize: Style.fontSizeMedium
        keyForwardTargets: [bar]
        Keys.onReturnPressed: event => event.accepted = true
        Keys.onEnterPressed: event => event.accepted = true
        onAccepted: bar.acceptTyped()
        onTextChanged: {
            suggestionPopup.highlightIndex = -1;
            bar.errorText = "";
        }
        onFocusStateChanged: hasFocus => {
            if (hasFocus || !bar.editing || bar.connecting)
                return;
            bar.reset();
        }
    }

    Component {
        id: connectingIndicator

        Item {
            implicitWidth: FileBrowserMetrics.pathIconSize + Style.spacingM - Style.spacingS
            implicitHeight: FileBrowserMetrics.pathIconSize

            DLoadingIndicator {
                size: FileBrowserMetrics.pathIconSize
            }
        }
    }
}
