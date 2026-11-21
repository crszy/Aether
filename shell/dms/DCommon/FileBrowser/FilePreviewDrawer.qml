pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets
import "FilePreview.js" as FilePreview

Item {
    id: drawer

    required property var body
    property bool open: false
    property real available: 0
    property real preferredWidth: 0
    property real dragWidth: 0
    property color chipColor: Style.chipSurface
    property var resolved: []
    property var strip: []
    property string focusPath: ""
    property int focusIndex: -1
    property var stripThumbs: ({})

    readonly property real gap: FileBrowserMetrics.contentSpacing
    readonly property real targetWidth: FilePreview.drawerWidth(available, grip.pressed ? dragWidth : preferredWidth, FileBrowserMetrics.previewDrawerMinWidth, FileBrowserMetrics.previewViewMinWidth, FileBrowserMetrics.previewDrawerRatio)
    readonly property bool shown: open && targetWidth > 0
    readonly property string folderName: FilePaths.baseName(body.directory.path) || body.directory.path
    readonly property real devicePixelRatio: Window.window?.devicePixelRatio ?? Screen.devicePixelRatio
    readonly property real stripRaster: FileBrowserMetrics.previewStripHeight * devicePixelRatio
    property real reveal: shown ? 1 : 0

    signal resized(real width)
    signal focusRequested

    function snapshot(item) {
        return {
            "path": item.path,
            "name": item.name,
            "iconName": item.iconName,
            "thumbnail": item.thumbnail,
            "mime": item.mime,
            "isDir": item.isDir,
            "size": item.size,
            "thumbnailable": item.thumbnailable
        };
    }

    function resolve() {
        const selection = body.selection;
        switch (selection.count) {
        case 0:
            strip = [];
            focusPath = "";
            break;
        case 1:
            strip = [];
            focusPath = selection.paths[0];
            break;
        default:
            strip = body.selectionInViewOrder().map(snapshot);
            if (!selection.contains(focusPath))
                focusPath = selection.contains(selection.cursorPath) ? selection.cursorPath : strip[0]?.path ?? "";
        }
        showFocus();
        stripSettle.restart();
    }

    function showFocus() {
        focusIndex = strip.findIndex(item => item.path === focusPath);
        const entry = focusPath === "" ? null : body.entryAt(body.directory.indexOfPath(focusPath));
        resolved = entry ? [entry] : [];
        if (focusIndex >= 0)
            stripView.positionViewAtIndex(focusIndex, ListView.Contain);
    }

    function preview(path) {
        focusPath = path;
        showFocus();
    }

    function step(direction) {
        if (strip.length < 2)
            return;
        const at = Math.max(0, focusIndex);
        preview(strip[(at + direction + strip.length) % strip.length].path);
    }

    function resize(direction) {
        if (!shown)
            return;
        if (direction === 0) {
            resized(0);
            return;
        }
        const towardFiles = LayoutMirroring.enabled ? direction > 0 : direction < 0;
        const wanted = targetWidth + (towardFiles ? 1 : -1) * FileBrowserMetrics.previewKeyStep;
        resized(FilePreview.drawerWidth(available, wanted, FileBrowserMetrics.previewDrawerMinWidth, FileBrowserMetrics.previewViewMinWidth, FileBrowserMetrics.previewDrawerRatio));
    }

    function handleKey(action, direction) {
        switch (action) {
        case "step":
            step(direction);
            return;
        case "scroll":
            content.item?.scrollPage(direction);
            return;
        case "resize":
            resize(direction);
            return;
        }
    }

    function requestStripThumbnails() {
        const backend = body.directory.backend;
        if (strip.length < 2 || !(body.directory.thumbnailsEnabled ?? true) || !FilePreview.readsAllowed(body.directory) || typeof backend?.thumbnails !== "function")
            return;
        const first = Math.max(0, stripView.indexAt(stripView.contentX + 1, stripView.height / 2));
        const found = stripView.indexAt(stripView.contentX + stripView.width - 1, stripView.height / 2);
        const last = found < 0 ? strip.length - 1 : found;
        const limit = body.directory.thumbnailSizeLimit ?? -1;
        const wanted = strip.slice(first, last + 1).filter(item => item.thumbnailable && !item.thumbnail && stripThumbs[item.path] === undefined && !(limit >= 0 && item.size > limit && item.mime.startsWith("image/"))).map(item => item.path);
        if (wanted.length === 0)
            return;
        backend.thumbnails(wanted, "normal", "", result => {
            if (!drawer || result.error)
                return;
            const merged = Object.assign({}, drawer.stripThumbs);
            for (const item of result.results ?? [])
                merged[item.path] = item.thumbnail ?? "";
            drawer.stripThumbs = merged;
        });
    }

    width: shown || reveal > 0 ? Math.round(targetWidth) : 0
    clip: true

    Behavior on reveal {
        enabled: FileBrowserMetrics.animationsEnabled && drawer.visible
        DAnim {
            duration: Style.expressiveDurations.expressiveFastSpatial
            easing.bezierCurve: Style.expressiveCurves.standard
        }
    }

    Item {
        id: sheet

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: Math.max(0, drawer.targetWidth - drawer.gap)
        transform: Translate {
            x: (1 - drawer.reveal) * (sheet.width + drawer.gap) * (drawer.LayoutMirroring.enabled ? -1 : 1)
        }

        Loader {
            id: content

            anchors.top: parent.top
            anchors.bottom: stripView.visible ? stripView.top : parent.bottom
            anchors.bottomMargin: stripView.visible ? drawer.gap : 0
            anchors.left: parent.left
            anchors.right: parent.right
            active: drawer.visible && (drawer.open || drawer.reveal > 0)
            onLoaded: drawer.resolve()
            sourceComponent: FilePreviewPane {
                visible: drawer.resolved.length > 0
                entries: drawer.resolved
                backend: drawer.body.directory.backend ?? null
                imageSizeLimit: drawer.body.directory.thumbnailSizeLimit ?? -1
                includeHidden: drawer.body.showHidden
                readsAllowed: FilePreview.readsAllowed(drawer.body.directory)
                thumbnailsEnabled: drawer.body.directory.thumbnailsEnabled ?? true
                chipColor: drawer.chipColor
                closable: true
                onCloseRequested: drawer.body.previewKeyRequested("toggle", 0)
                caption: drawer.strip.length > 1 && drawer.focusIndex >= 0 ? I18n.tr("%1 of %2", "position of the previewed item among the selected items").arg(drawer.focusIndex + 1).arg(drawer.strip.length) : ""
            }

            Rectangle {
                anchors.fill: parent
                visible: content.active && drawer.resolved.length === 0
                radius: FileBrowserMetrics.previewCardRadius
                color: drawer.chipColor
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - FileBrowserMetrics.previewCardPadding * 2
                visible: content.active && drawer.resolved.length === 0
                spacing: Style.spacingS

                FileIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: FileBrowserMetrics.previewIconSize
                    iconName: "folder"
                    color: Style.primary
                }

                StyledText {
                    width: parent.width
                    text: drawer.folderName
                    color: Style.surfaceText
                    font.pixelSize: Style.fontSizeLarge
                    font.weight: Style.fontWeightMedium
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                }

                StyledText {
                    width: parent.width
                    opacity: drawer.body.directory.loading ? 0 : 1
                    text: FileFormat.count(drawer.body.directory.total)
                    color: Style.surfaceVariantText
                    font.pixelSize: Style.fontSizeMedium
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            TapHandler {
                onTapped: {
                    drawer.focusRequested();
                    drawer.body.focusBody();
                }
            }
        }

        DCarousel {
            id: stripView

            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: FileBrowserMetrics.previewStripHeight
            visible: content.active && drawer.strip.length > 1
            model: visible ? drawer.strip.length : 0
            smallItemWidth: FileBrowserMetrics.previewStripSmallWidth
            itemRadius: Style.cornerRadiusM
            onContentXChanged: stripSettle.restart()
            onCountChanged: {
                if (drawer.focusIndex >= 0)
                    positionViewAtIndex(drawer.focusIndex, ListView.Contain);
            }

            delegate: DCarouselItem {
                id: tile

                required property int index

                readonly property var item: drawer.strip[index] ?? null
                readonly property string thumbnail: item ? (item.thumbnail || drawer.stripThumbs[item.path] || "") : ""

                tooltipText: item?.name ?? ""
                onClicked: {
                    drawer.preview(item.path);
                    drawer.focusRequested();
                    drawer.body.focusBody();
                }

                Image {
                    anchors.fill: parent
                    visible: status === Image.Ready
                    source: tile.thumbnail === "" ? "" : FilePaths.toFileUrl(tile.thumbnail)
                    sourceSize.width: drawer.stripRaster
                    sourceSize.height: drawer.stripRaster
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }

                FileIcon {
                    anchors.centerIn: parent
                    visible: tile.thumbnail === ""
                    size: Style.iconSizeLarge
                    iconName: tile.item?.iconName ?? ""
                    color: tile.item?.isDir ? Style.primary : Style.surfaceText
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: tile.maskWidth
                    height: parent.height
                    radius: Math.min(stripView.itemRadius, width / 2)
                    visible: tile.item?.path === drawer.focusPath
                    color: "transparent"
                    border.width: Style.outlineWidthFocused
                    border.color: Style.primary
                }
            }
        }
    }

    Timer {
        id: resolveLater

        interval: drawer.body.selection.count > 1 ? FileBrowserMetrics.settleInterval : 0
        onTriggered: drawer.resolve()
    }

    Timer {
        id: stripSettle

        interval: FileBrowserMetrics.settleInterval
        onTriggered: drawer.requestStripThumbnails()
    }

    Connections {
        target: content.item ? drawer.body.selection : null

        function onPathsChanged() {
            resolveLater.restart();
        }

        function onCursorPathChanged() {
            const selection = drawer.body.selection;
            if (selection.contains(selection.cursorPath))
                drawer.focusPath = selection.cursorPath;
            resolveLater.restart();
        }
    }

    Connections {
        target: content.item ? drawer.body.directory.entries : null

        function onRowsInserted() {
            resolveLater.restart();
        }

        function onRowsRemoved() {
            resolveLater.restart();
        }

        function onModelReset() {
            resolveLater.restart();
        }

        function onDataChanged() {
            resolveLater.restart();
        }
    }

    Item {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: sheet.left
        width: drawer.gap
        visible: drawer.shown && drawer.reveal === 1

        Rectangle {
            anchors.centerIn: parent
            width: Style.outlineWidthFocused
            height: FileBrowserMetrics.splitterHandleHeight
            radius: Style.cornerRadiusFull
            color: Style.primary
            visible: grip.containsMouse || grip.pressed
        }

        MouseArea {
            id: grip

            property real pressX: 0
            property real startWidth: 0
            property bool moved: false

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SplitHCursor
            acceptedButtons: Qt.LeftButton
            onPressed: mouse => {
                pressX = mapToItem(null, mouse.x, 0).x;
                startWidth = drawer.targetWidth;
                drawer.dragWidth = startWidth;
                moved = false;
            }
            onPositionChanged: mouse => {
                if (!pressed)
                    return;
                const delta = mapToItem(null, mouse.x, 0).x - pressX;
                moved = true;
                drawer.dragWidth = Math.max(FileBrowserMetrics.previewDrawerMinWidth, startWidth + (drawer.LayoutMirroring.enabled ? delta : -delta));
            }
            onReleased: {
                if (!moved)
                    return;
                drawer.resized(FilePreview.drawerWidth(drawer.available, drawer.dragWidth, FileBrowserMetrics.previewDrawerMinWidth, FileBrowserMetrics.previewViewMinWidth, FileBrowserMetrics.previewDrawerRatio));
            }
            onDoubleClicked: drawer.resized(0)
        }
    }
}
