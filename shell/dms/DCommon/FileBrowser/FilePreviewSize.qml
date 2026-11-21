pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common

Item {
    id: viewer

    property var pane: null
    property int request: 0
    property int childCount: -1
    property real bytes: -1
    property bool scanning: false
    property string scanId: ""
    property var early: ({})

    readonly property var folder: pane?.settledEntries[0] ?? null
    readonly property bool showsContent: false
    readonly property string note: ""
    readonly property string sizeText: {
        const parts = [];
        if (childCount >= 0)
            parts.push(FileFormat.count(childCount));
        if (bytes >= 0)
            parts.push(scanning ? `${FileFormat.size(bytes)} …` : FileFormat.size(bytes));
        return parts.join(" · ");
    }
    readonly property var rows: sizeText === "" ? [] : [
        {
            "id": "size",
            "label": FileColumns.label("size"),
            "value": sizeText
        }
    ]

    function stop() {
        request++;
        early = ({});
        scanning = false;
        if (scanId === "")
            return;
        pane?.backend?.cancelScan?.(scanId);
        scanId = "";
    }

    function load() {
        stop();
        childCount = -1;
        bytes = -1;
        if (!folder)
            return;
        countChildren(folder.path);
        scan(folder.path);
    }

    function countChildren(path) {
        if (typeof pane.backend?.count !== "function")
            return;
        const current = request;
        pane.backend.count([path], pane.includeHidden, result => {
            const counted = result.counts?.[path];
            if (!viewer || current !== viewer.request || !counted || counted.code)
                return;
            viewer.childCount = counted.count;
        });
    }

    function scan(path) {
        const backend = pane.backend;
        if (!pane.readsAllowed || typeof backend?.scanSize !== "function" || typeof backend?.sizeEvent !== "function")
            return;
        const current = request;
        scanning = true;
        bytes = 0;
        backend.scanSize([path], result => {
            if (!viewer || current !== viewer.request) {
                if (result.scanId)
                    backend.cancelScan?.(result.scanId);
                return;
            }
            if (result.error || !result.scanId) {
                viewer.scanning = false;
                viewer.bytes = -1;
                return;
            }
            viewer.scanId = result.scanId;
            const waiting = viewer.early[result.scanId];
            viewer.early = ({});
            if (waiting)
                viewer.apply(waiting);
        });
    }

    function apply(event) {
        bytes = event.bytes;
        if (!event.done)
            return;
        scanning = false;
        scanId = "";
    }

    function receive(event) {
        if (!scanning)
            return;
        if (scanId !== "") {
            if (event.scanId === scanId)
                apply(event);
            return;
        }
        const next = Object.assign({}, early);
        next[event.scanId] = event;
        early = next;
    }

    Connections {
        target: viewer.pane

        function onRevisionChanged() {
            viewer.load();
        }

        function onCurrentChanged() {
            if (!viewer.pane.current)
                viewer.stop();
        }
    }

    Connections {
        target: viewer.pane?.backend ?? null
        ignoreUnknownSignals: true

        function onSizeEvent(event) {
            viewer.receive(event);
        }
    }

    Component.onCompleted: load()
    Component.onDestruction: stop()
}
