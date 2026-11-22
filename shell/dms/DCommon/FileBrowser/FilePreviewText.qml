pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets
import "FilePreview.js" as FilePreview

Item {
    id: viewer

    property var pane: null
    property int request: 0
    property var lines: []
    property bool binary: false
    property bool truncated: false
    property real fileSize: 0
    property string loadedPath: ""

    readonly property var entry: pane?.settledEntries[0] ?? null
    readonly property bool showsContent: lines.length > 1 || (lines[0] ?? "") !== ""
    readonly property var rows: []
    readonly property string note: {
        if (binary)
            return I18n.tr("Binary file", "preview column note for files without readable text");
        if (truncated)
            return I18n.tr("First %1 of %2", "preview column note when only the start of the file is shown").arg(FileFormat.size(FileBrowserMetrics.previewBytes)).arg(FileFormat.size(fileSize));
        return "";
    }

    function scrollPage(direction) {
        list.contentY = Math.max(list.originY, Math.min(list.originY + list.contentHeight - list.height, list.contentY + direction * list.height));
    }

    function stop() {
        request++;
        lines = [];
        binary = false;
        truncated = false;
        loadedPath = "";
    }

    function load() {
        const current = ++request;
        if (!entry || typeof pane.backend?.preview !== "function") {
            stop();
            return;
        }
        const target = entry.path;
        pane.backend.preview(target, FileBrowserMetrics.previewBytes, result => {
            if (!viewer || current !== viewer.request)
                return;
            if (result.error) {
                viewer.stop();
                return;
            }
            viewer.binary = result.binary === true;
            viewer.truncated = result.truncated === true;
            viewer.fileSize = result.size ?? 0;
            viewer.lines = FilePreview.textLines(result.text ?? "", FileBrowserMetrics.previewLineChunk);
            if (viewer.loadedPath !== target)
                list.positionViewAtBeginning();
            viewer.loadedPath = target;
        });
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

    Component.onCompleted: load()

    Item {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.min(parent.height, list.contentHeight)
        visible: viewer.showsContent

        DListView {
            id: list

            anchors.fill: parent
            clip: true
            model: viewer.lines.length
            delegate: StyledText {
                required property int index

                width: ListView.view.width
                text: viewer.lines[index] ?? ""
                isMonospace: true
                color: Style.surfaceText
                font.pixelSize: Style.fontSizeSmall
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                textFormat: Text.PlainText
            }
        }
    }
}
