pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets
import "FilePreview.js" as FilePreview

Item {
    id: pane

    property var entries: []
    property var sourceModel: null
    property var backend: null
    property real imageSizeLimit: -1
    property bool includeHidden: false
    property bool readsAllowed: true
    property bool thumbnailsEnabled: true
    property color chipColor: Style.chipSurface
    property string caption: ""
    property bool closable: false
    readonly property color surfaceColor: chipColor
    property var viewerSources: ({
            "image": "FilePreviewImage.qml",
            "svg": "FilePreviewImage.qml",
            "thumbnail": "FilePreviewImage.qml",
            "text": "FilePreviewText.qml",
            "folder": "FilePreviewSize.qml"
        })

    property var shownEntries: []

    readonly property var entry: shownEntries[0] ?? null
    readonly property string viewer: {
        const chosen = FilePreview.viewerFor(entry, imageSizeLimit);
        return readsAllowed || chosen === "folder" ? chosen : "";
    }
    readonly property string identityKey: `${readsAllowed}:${imageSizeLimit}:${includeHidden}:${thumbnailsEnabled}\n` + shownEntries.map(item => `${item.path}:${item.mime}:${item.thumbnailable}`).join("\n")
    readonly property string versionKey: shownEntries.map(item => `${item.mtimeMs}:${item.size}`).join("\n")
    readonly property var viewerItem: current ? viewerLoader.item : null
    readonly property var rows: {
        const extra = viewerItem?.rows ?? [];
        const replaced = ({});
        for (const row of extra)
            replaced[row.id] = row;
        const head = ["type", "size"];
        const tail = ["modified", "created", "owner", "permissions"];
        const make = id => {
            if (replaced[id])
                return replaced[id];
            if (!entry)
                return null;
            const value = FileColumns.value(id, entry);
            const pending = id === "size" && entry.isDir && value === "";
            return {
                "id": id,
                "label": FileColumns.label(id),
                "value": pending ? "…" : value
            };
        };
        const middle = extra.filter(row => !head.includes(row.id) && !tail.includes(row.id));
        return head.map(make).concat(middle, tail.map(make)).filter(row => row && row.value !== "");
    }

    property var settledEntries: []
    property string settledViewer: ""
    property bool current: false
    property real lastMove: 0
    property int revision: 0

    signal closeRequested

    function scrollPage(direction) {
        if (typeof viewerItem?.scrollPage === "function") {
            viewerItem.scrollPage(direction);
            return;
        }
        info.contentY = Math.max(0, Math.min(info.contentHeight - info.height, info.contentY + direction * info.height));
    }

    function adoptEntries() {
        adopt.stop();
        if (entries.length === 0) {
            shownEntries = [];
            return;
        }
        const next = Object.assign({}, entries[0]);
        if (shownEntries.length === 1 && JSON.stringify(shownEntries[0]) === JSON.stringify(next))
            return;
        shownEntries = [next];
    }

    function settleNow() {
        adoptEntries();
        settle.stop();
        refresh.stop();
        settledEntries = shownEntries;
        settledViewer = viewer;
        current = true;
        const name = viewerSources[viewer] ?? "";
        const source = name === "" ? "" : String(Qt.resolvedUrl(name));
        if (String(viewerLoader.source) === source) {
            revision++;
            return;
        }
        if (source === "") {
            viewerLoader.source = "";
            return;
        }
        viewerLoader.setSource(source, {
            "pane": pane
        });
    }

    onEntriesChanged: adopt.restart()
    onIdentityKeyChanged: {
        current = false;
        refresh.stop();
        const now = Date.now();
        settle.interval = now - lastMove < FileBrowserMetrics.settleInterval ? FileBrowserMetrics.settleInterval : 0;
        lastMove = now;
        settle.restart();
    }
    onVersionKeyChanged: {
        if (!current || refresh.running)
            return;
        refresh.start();
    }
    Component.onCompleted: adopt.restart()

    Connections {
        target: pane.sourceModel

        function onDataChanged() {
            adopt.restart();
        }
    }

    Timer {
        id: adopt

        interval: 0
        onTriggered: pane.adoptEntries()
    }

    Timer {
        id: settle

        interval: FileBrowserMetrics.settleInterval
        onTriggered: pane.settleNow()
    }

    Timer {
        id: refresh

        interval: FileBrowserMetrics.previewRefreshInterval
        onTriggered: pane.settleNow()
    }

    Rectangle {
        anchors.fill: parent
        radius: FileBrowserMetrics.previewCardRadius
        color: pane.surfaceColor
    }

    Item {
        id: stage

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: info.top
        anchors.topMargin: FileBrowserMetrics.previewCardPadding
        anchors.leftMargin: FileBrowserMetrics.previewCardPadding
        anchors.rightMargin: FileBrowserMetrics.previewCardPadding
        anchors.bottomMargin: Style.spacingM
        visible: pane.shownEntries.length > 0

        readonly property int iconSize: Math.max(0, Math.min(FileBrowserMetrics.previewIconSize, width, height))
        readonly property bool showsContent: pane.viewerItem?.showsContent ?? false
        readonly property string stand: {
            if (!pane.entry || !["image", "svg", "thumbnail"].includes(pane.viewer))
                return "";
            if (pane.entry.thumbnail)
                return FilePaths.toFileUrl(pane.entry.thumbnail);
            return pane.viewer === "svg" ? FilePaths.toFileUrl(pane.entry.path) : "";
        }
        readonly property bool standing: stand !== "" && standIn.status === Image.Ready
        readonly property real standRaster: iconSize * (Window.window?.devicePixelRatio ?? Screen.devicePixelRatio)

        Image {
            id: standIn

            anchors.fill: parent
            visible: stage.standing && !stage.showsContent
            source: stage.stand
            sourceSize.width: stage.standRaster
            sourceSize.height: stage.standRaster
            fillMode: Image.PreserveAspectFit
            asynchronous: false
            cache: true
        }

        FileItemVisual {
            anchors.centerIn: parent
            visible: pane.entry !== null && !stage.showsContent && !stage.standing
            iconSize: stage.iconSize
            iconName: pane.entry?.iconName ?? ""
            path: pane.entry?.path ?? ""
            thumbnail: pane.entry?.thumbnail ?? ""
            mime: pane.entry?.mime ?? ""
            isDir: pane.entry?.isDir ?? false
            isSymlink: pane.entry?.isSymlink ?? false
            symlinkBroken: pane.entry?.symlinkBroken ?? false
            untrusted: pane.entry?.untrusted ?? false
            unreadable: pane.entry?.unreadable ?? false
            hidden: pane.entry?.hidden ?? false
        }

        Loader {
            id: viewerLoader

            anchors.fill: parent
            visible: pane.current
        }
    }

    DFlickable {
        id: info

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: FileBrowserMetrics.previewCardPadding
        anchors.rightMargin: FileBrowserMetrics.previewCardPadding
        anchors.bottomMargin: FileBrowserMetrics.previewCardPadding
        height: Math.min(infoColumn.implicitHeight, pane.height * FileBrowserMetrics.previewInfoRatio)
        visible: pane.shownEntries.length > 0
        contentWidth: width
        contentHeight: infoColumn.implicitHeight
        interactive: contentHeight > height
        clip: true

        Column {
            id: infoColumn

            width: parent.width
            spacing: Style.spacingM

            Column {
                width: parent.width
                spacing: Style.spacingXS

                StyledText {
                    width: parent.width
                    text: pane.entry ? FileFormat.shownName("", pane.entry.name, pane.entry.isDir ?? false) : ""
                    color: Style.surfaceText
                    font.pixelSize: Style.fontSizeLarge
                    font.weight: Style.fontWeightMedium
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }

                StyledText {
                    width: parent.width
                    visible: text !== ""
                    text: [pane.caption, pane.viewerItem?.note ?? ""].filter(part => part !== "").join(" · ")
                    color: Style.surfaceVariantText
                    font.pixelSize: Style.fontSizeSmall
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
            }

            Column {
                width: parent.width
                spacing: Style.spacingXS

                Repeater {
                    model: pane.rows

                    Item {
                        id: detail

                        required property var modelData

                        width: parent.width
                        height: Math.max(label.implicitHeight, content.implicitHeight)

                        StyledText {
                            id: label

                            anchors.left: parent.left
                            width: Math.min(FileBrowserMetrics.detailLabelWidth, parent.width / 2)
                            text: detail.modelData.label
                            color: Style.surfaceVariantText
                            font.pixelSize: Style.fontSizeSmall
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }

                        StyledText {
                            id: content

                            anchors.left: label.right
                            anchors.leftMargin: Style.spacingM
                            anchors.right: parent.right
                            text: detail.modelData.value
                            color: Style.surfaceText
                            font.pixelSize: Style.fontSizeSmall
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }
    }

    DActionButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Style.spacingS
        visible: pane.closable
        iconName: "close"
        iconSize: Style.iconSizeSmall
        iconColor: Style.surfaceVariantText
        backgroundColor: pane.surfaceColor
        tooltipText: I18n.tr("Hide preview", "tooltip on the button closing the preview drawer")
        onClicked: pane.closeRequested()
    }
}
