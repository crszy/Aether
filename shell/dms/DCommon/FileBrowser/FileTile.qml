pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

FileItemSurface {
    id: tile

    required property string name
    required property bool isSymlink
    required property bool symlinkBroken
    required property real size
    required property string iconName
    required property string thumbnail
    required property string mime
    required property string displayName
    required property bool untrusted
    required property bool unreadable
    required property bool hidden

    readonly property string highlight: view?.nameHighlight ?? ""

    hitTargets: [visual, nameLabel, sizeLabel]

    width: Math.max(0, (view?.cellWidth ?? 0) - FileBrowserMetrics.gridGap)
    height: Math.max(0, (view?.cellHeight ?? 0) - FileBrowserMetrics.gridGap)
    contentRadius: FileBrowserMetrics.gridTileRadius
    selected: view?.selection?.contains(path) ?? false
    cursor: view?.selection?.showsCursor(path) ?? false
    cut: view?.cutSet?.[path] === true
    renaming: (view?.renamingPath ?? "") === path

    Column {
        anchors.top: parent.top
        anchors.topMargin: FileBrowserMetrics.gridTileVerticalPadding
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - FileBrowserMetrics.gridTilePadding * 2
        spacing: FileBrowserMetrics.gridNameSpacing

        FileItemVisual {
            id: visual

            anchors.horizontalCenter: parent.horizontalCenter
            iconSize: tile.view?.iconSize ?? FileBrowserMetrics.gridIconSizes[FileBrowserMetrics.defaultGridZoom]
            iconName: tile.iconName
            thumbnail: tile.thumbnail
            path: tile.path
            mime: tile.mime
            isDir: tile.isDir
            isSymlink: tile.isSymlink
            symlinkBroken: tile.symlinkBroken
            untrusted: tile.untrusted
            unreadable: tile.unreadable
            hidden: tile.hidden
            cut: tile.cut
            selected: tile.selected
            tint: tile.contentColor
        }

        StyledText {
            id: nameLabel

            readonly property string shown: FileFormat.shownName(tile.displayName, tile.name, tile.isDir)

            width: parent.width
            visible: !tile.renaming
            opacity: tile.cut ? Style.pendingOpacity : 1
            text: tile.highlight === "" ? (tile.view?.fitName(shown, tile.isDir) ?? shown) : FileFormat.highlight(shown, tile.highlight, tile.selected ? tile.contentColor : Style.primary)
            textFormat: tile.highlight === "" ? Text.PlainText : Text.StyledText
            color: tile.contentColor
            font.pixelSize: Style.fontSizeSmall
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.Wrap
        }

        StyledText {
            id: sizeLabel

            width: parent.width
            visible: !tile.renaming && !tile.isDir && tile.size >= 0
            text: FileFormat.size(tile.size)
            color: tile.supportingColor
            font.pixelSize: Style.fontSizeSmall
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Loader {
            width: parent.width
            active: tile.renaming
            sourceComponent: InlineRename {
                width: parent.width
                name: tile.name
                isDir: tile.isDir
                onCommitted: name => tile.view.renameSubmitted(tile.path, name)
                onCancelled: tile.view.renameCancelled()
                Component.onCompleted: begin()
            }
        }
    }
}
