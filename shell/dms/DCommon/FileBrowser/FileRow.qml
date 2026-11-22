pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

FileItemSurface {
    id: row

    required property string name
    required property bool isSymlink
    required property bool symlinkBroken
    required property real size
    required property real mtimeMs
    required property real ctimeMs
    required property string mode
    required property string owner
    required property string iconName
    required property string thumbnail
    required property string mime
    required property string extension
    required property string displayName
    required property bool untrusted
    required property bool unreadable
    required property bool hidden
    required property var model

    readonly property string highlight: view?.nameHighlight ?? ""
    readonly property int childCount: view?.childCounts?.[path] ?? -1
    readonly property var entry: ({
            "path": path,
            "isDir": isDir,
            "symlinkBroken": symlinkBroken,
            "extension": extension,
            "mime": mime,
            "size": size,
            "mtimeMs": mtimeMs,
            "ctimeMs": ctimeMs,
            "owner": owner,
            "mode": mode,
            "originalPath": model.originalPath ?? "",
            "deletedMs": model.deletedMs ?? 0,
            "usedMs": model.usedMs ?? 0
        })

    hitTargets: [visual, nameLabel, chevron].concat(Array.from(columns.children))

    width: view?.width ?? 0
    height: view?.rowHeight ?? FileBrowserMetrics.listRowHeight
    selected: view?.selection?.contains(path) ?? false
    cursor: view?.selection?.showsCursor(path) ?? false
    cut: view?.cutSet?.[path] === true
    renaming: (view?.renamingPath ?? "") === path

    FileItemVisual {
        id: visual

        anchors.left: parent.left
        anchors.leftMargin: FileBrowserMetrics.listRowPadding
        anchors.verticalCenter: parent.verticalCenter
        iconSize: row.view?.iconSize ?? Style.iconSize
        iconName: row.iconName
        thumbnail: row.thumbnail
        path: row.path
        mime: row.mime
        isDir: row.isDir
        isSymlink: row.isSymlink
        symlinkBroken: row.symlinkBroken
        untrusted: row.untrusted
        unreadable: row.unreadable
        hidden: row.hidden
        cut: row.cut
        selected: row.selected
        tint: row.contentColor
    }

    Row {
        id: columns

        anchors.right: parent.right
        anchors.rightMargin: FileBrowserMetrics.listRowPadding
        anchors.verticalCenter: parent.verticalCenter
        spacing: FileBrowserMetrics.columnGap

        Repeater {
            model: row.view?.columnIds ?? []

            StyledText {
                required property string modelData

                width: row.view.columnWidth(modelData)
                horizontalAlignment: FileColumns.specFor(modelData)?.align ?? Text.AlignLeft
                text: FileColumns.value(modelData, row.entry, row.childCount)
                color: row.supportingColor
                font.pixelSize: Style.fontSizeSmall
                elide: Text.ElideRight
            }
        }
    }

    Loader {
        id: chevron

        anchors.right: columns.left
        anchors.verticalCenter: parent.verticalCenter
        active: row.isDir && (row.view?.chevrons ?? false)
        width: active ? Style.iconSizeMedium : 0
        sourceComponent: DIcon {
            name: "chevron_right"
            size: Style.iconSizeMedium
            color: row.supportingColor
        }
    }

    StyledText {
        id: nameLabel

        anchors.left: visual.right
        anchors.leftMargin: FileBrowserMetrics.listRowGap
        anchors.right: chevron.left
        anchors.rightMargin: FileBrowserMetrics.columnGap
        anchors.verticalCenter: parent.verticalCenter
        visible: !row.renaming
        opacity: row.cut ? Style.pendingOpacity : 1
        text: FileFormat.highlight(FileFormat.shownName(row.displayName, row.name, row.isDir), row.highlight, row.selected ? row.contentColor : Style.primary)
        textFormat: row.highlight === "" ? Text.PlainText : Text.StyledText
        color: row.contentColor
        font.pixelSize: Style.fontSizeMedium
        wrapMode: Text.NoWrap
        elide: Text.ElideMiddle
    }

    Loader {
        anchors.left: visual.right
        anchors.leftMargin: FileBrowserMetrics.listRowGap
        anchors.right: chevron.left
        anchors.rightMargin: FileBrowserMetrics.columnGap
        anchors.verticalCenter: parent.verticalCenter
        active: row.renaming
        sourceComponent: InlineRename {
            name: row.name
            isDir: row.isDir
            onCommitted: name => row.view.renameSubmitted(row.path, name)
            onCancelled: row.view.renameCancelled()
            Component.onCompleted: begin()
        }
    }
}
