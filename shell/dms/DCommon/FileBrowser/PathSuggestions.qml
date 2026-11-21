pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

Popup {
    id: root

    property Item anchorItem: null
    property var rows: []
    property string errorText: ""
    property int highlightIndex: -1
    readonly property real rowHeight: Style.menuRowHeight

    signal chosen(var row)

    function move(delta) {
        const count = rows.length;
        if (count === 0)
            return;
        let next = highlightIndex + delta;
        if (next >= count)
            next = -1;
        if (next < -1)
            next = count - 1;
        highlightIndex = next;
    }

    function page(delta) {
        if (rows.length === 0)
            return;
        highlightIndex = Math.max(0, Math.min(rows.length - 1, highlightIndex + delta * FileBrowserMetrics.suggestionRows));
    }

    function highlightedRow() {
        return highlightIndex >= 0 && highlightIndex < rows.length ? rows[highlightIndex] : null;
    }

    parent: anchorItem
    x: I18n.isRtl ? (anchorItem?.width ?? 0) - width : 0
    y: (anchorItem?.height ?? 0) + Style.spacingXS
    width: Math.max(anchorItem?.width ?? 0, FileBrowserMetrics.suggestionMinWidth)
    margins: 0
    padding: Style.spacingXS
    modal: false
    dim: false
    focus: false
    closePolicy: Popup.NoAutoClose
    onRowsChanged: {
        if (highlightIndex >= rows.length)
            highlightIndex = -1;
        list.resetScroll();
    }
    onHighlightIndexChanged: {
        if (highlightIndex >= 0)
            list.revealIndex(highlightIndex);
    }

    background: Item {
        ElevationShadow {
            anchors.fill: parent
            level: Style.elevationLevel2
            fallbackOffset: Style.spacingXS
            targetRadius: Style.cornerRadiusL
            targetColor: Style.surfaceContainer
            borderColor: Style.outlineMedium
            borderWidth: Style.layerOutlineWidth
            shadowEnabled: Style.elevationEnabled && Style.popoutElevationEnabled
        }
    }

    contentItem: Column {
        LayoutMirroring.enabled: I18n.isRtl
        LayoutMirroring.childrenInherit: true
        spacing: 0

        Item {
            width: parent.width
            height: root.rowHeight
            visible: root.errorText !== ""

            DIcon {
                id: errorIcon

                anchors.left: parent.left
                anchors.leftMargin: Style.spacingM
                anchors.verticalCenter: parent.verticalCenter
                name: "error"
                size: Style.menuItemIconSize
                color: Style.error
            }

            StyledText {
                anchors.left: errorIcon.right
                anchors.leftMargin: Style.spacingM
                anchors.right: parent.right
                anchors.rightMargin: Style.spacingM
                anchors.verticalCenter: parent.verticalCenter
                text: root.errorText
                color: Style.error
                font.pixelSize: Style.fontSizeMedium
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }
        }

        DListView {
            id: list

            width: parent.width
            height: Math.min(root.rows.length, FileBrowserMetrics.suggestionRows) * root.rowHeight
            visible: root.rows.length > 0
            clip: true
            model: root.rows

            delegate: StyledButton {
                id: entry

                required property var modelData
                required property int index

                width: ListView.view.width
                implicitHeight: root.rowHeight
                radius: Style.cornerRadiusS
                color: "transparent"
                focusPolicy: Qt.NoFocus
                onClicked: root.chosen(modelData)

                StateLayer {
                    control: entry
                    hovered: entry.hovered
                    focused: entry.index === root.highlightIndex
                    stateColor: Style.surfaceText
                }

                DIcon {
                    id: entryIcon

                    anchors.left: parent.left
                    anchors.leftMargin: Style.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    name: entry.modelData.kind === "visited" ? "history" : "folder"
                    size: Style.menuItemIconSize
                    color: Style.surfaceVariantText
                }

                StyledText {
                    id: entryHint

                    anchors.right: parent.right
                    anchors.rightMargin: Style.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width * FileBrowserMetrics.suggestionHintRatio)
                    visible: entry.modelData.hint !== ""
                    text: FileFormat.markRanges(entry.modelData.hint, entry.modelData.hintMarks, Style.primary)
                    textFormat: Text.StyledText
                    color: Style.surfaceVariantText
                    font.pixelSize: Style.fontSizeSmall
                    horizontalAlignment: Text.AlignRight
                    wrapMode: Text.NoWrap
                    elide: Text.ElideLeft
                }

                StyledText {
                    anchors.left: entryIcon.right
                    anchors.leftMargin: Style.spacingM
                    anchors.right: entryHint.visible ? entryHint.left : parent.right
                    anchors.rightMargin: Style.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    text: FileFormat.markRanges(entry.modelData.name, entry.modelData.nameMarks, Style.primary)
                    textFormat: Text.StyledText
                    color: Style.surfaceText
                    font.pixelSize: Style.fontSizeMedium
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                }
            }
        }
    }
}
