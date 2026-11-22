pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import "FilePreview.js" as FilePreview

Item {
    id: viewer

    property var pane: null
    property int request: 0
    property int pixelWidth: 0
    property int pixelHeight: 0
    property bool fromThumbnail: false
    property string shown: ""
    property var readyKeys: ({})
    property var failedKeys: ({})

    readonly property var entry: pane?.settledEntries[0] ?? null
    readonly property real devicePixelRatio: Window.window?.devicePixelRatio ?? Screen.devicePixelRatio
    readonly property bool showsContent: shown !== "" && readyKeys[shown] === true
    readonly property string note: ""
    readonly property var rows: pixelWidth > 0 ? [
        {
            "id": "dimensions",
            "label": I18n.tr("Dimensions", "preview row label for the width and height of an image"),
            "value": I18n.tr("%1 × %2", "image width and height in pixels").arg(pixelWidth).arg(pixelHeight)
        }
    ] : []

    function rasterSide(side) {
        return Math.ceil(Math.max(side > 0 ? side : FileBrowserMetrics.previewIconSize, 1) * devicePixelRatio);
    }

    function stop() {
        request++;
        shown = "";
    }

    function load() {
        stop();
        pixelWidth = 0;
        pixelHeight = 0;
        fromThumbnail = false;
        if (!entry)
            return;
        switch (pane.settledViewer) {
        case "thumbnail":
            loadThumbnail();
            return;
        case "image":
            measure();
            break;
        }
        show(FilePaths.toFileUrl(entry.path));
    }

    function show(source) {
        const rasterWidth = rasterSide(width);
        const rasterHeight = rasterSide(height);
        const key = `${rasterWidth}x${rasterHeight} ${source}`;
        let index = -1;
        for (let i = 0; i < pool.count && index < 0; i++) {
            if (pool.get(i).key === key)
                index = i;
        }
        switch (index) {
        case -1:
            pool.insert(0, {
                "key": key,
                "url": source,
                "rasterWidth": rasterWidth,
                "rasterHeight": rasterHeight
            });
            trim();
            break;
        case 0:
            break;
        default:
            pool.move(index, 0, 1);
        }
        shown = key;
        if (failedKeys[key] === true)
            fallBack(key);
    }

    function trim() {
        if (pool.count <= FileBrowserMetrics.previewImagePool)
            return;
        const ready = Object.assign({}, readyKeys);
        const failed = Object.assign({}, failedKeys);
        for (let i = FileBrowserMetrics.previewImagePool; i < pool.count; i++) {
            delete ready[pool.get(i).key];
            delete failed[pool.get(i).key];
        }
        pool.remove(FileBrowserMetrics.previewImagePool, pool.count - FileBrowserMetrics.previewImagePool);
        readyKeys = ready;
        failedKeys = failed;
    }

    function track(key, status) {
        const ready = Object.assign({}, readyKeys);
        if (status === Image.Ready)
            ready[key] = true;
        else
            delete ready[key];
        readyKeys = ready;
        if (status !== Image.Error)
            return;
        const failed = Object.assign({}, failedKeys);
        failed[key] = true;
        failedKeys = failed;
        if (key === shown)
            fallBack(key);
    }

    function fallBack(key) {
        if (fromThumbnail || pane?.settledViewer !== "image")
            return;
        loadThumbnail();
    }

    function measure() {
        if (typeof pane.backend?.imageSize !== "function")
            return;
        const current = request;
        pane.backend.imageSize(entry.path, result => {
            if (!viewer || current !== viewer.request || result.error)
                return;
            viewer.pixelWidth = result.width ?? 0;
            viewer.pixelHeight = result.height ?? 0;
        });
    }

    function loadThumbnail() {
        fromThumbnail = true;
        if (!pane.thumbnailsEnabled || typeof pane.backend?.thumbnails !== "function")
            return;
        const current = request;
        const target = entry.path;
        const size = FilePreview.thumbnailSizeFor(Math.max(rasterSide(width), rasterSide(height)));
        pane.backend.thumbnails([target], size, "", result => {
            if (!viewer || current !== viewer.request || result.error)
                return;
            const found = (result.results ?? []).find(item => item.path === target);
            if (!found?.thumbnail)
                return;
            viewer.show(FilePaths.toFileUrl(found.thumbnail));
        });
    }

    Connections {
        target: viewer.pane

        function onRevisionChanged() {
            reload.restart();
        }

        function onCurrentChanged() {
            if (!viewer.pane.current)
                viewer.stop();
        }
    }

    Component.onCompleted: reload.restart()

    Timer {
        id: reload

        interval: 0
        onTriggered: viewer.load()
    }

    ListModel {
        id: pool
    }

    Repeater {
        model: pool

        Image {
            required property string key
            required property string url
            required property int rasterWidth
            required property int rasterHeight

            anchors.fill: parent
            visible: key === viewer.shown && status === Image.Ready
            source: url
            sourceSize.width: rasterWidth
            sourceSize.height: rasterHeight
            asynchronous: true
            cache: false
            autoTransform: true
            fillMode: Image.PreserveAspectFit
            onStatusChanged: viewer.track(key, status)
        }
    }
}
