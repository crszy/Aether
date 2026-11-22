pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

Item {
    id: header

    property var listColumns: []
    property var columnIds: []
    property var widths: ({})
    property string sortKey: "name"
    property bool sortDescending: false
    property var columnSortKeys: ({})
    property real nameIndent: 0
    property color backgroundColor: "transparent"

    signal sortRequested(string key)
    signal columnResized(string id, real width)
    signal columnsMenuRequested(real pointX, real pointY)

    function columnWidth(id) {
        return FileColumns.widthFor(id, widths);
    }

    implicitHeight: FileBrowserMetrics.columnHeaderHeight
    clip: true

    Rectangle {
        anchors.fill: parent
        color: header.backgroundColor
    }

    HoverHandler {
        id: headerHover
    }

    component HeaderCell: Item {
        id: cell

        property string columnId: ""
        property string sortsBy: ""
        property int align: Text.AlignLeft

        readonly property bool active: header.sortKey === sortsBy && sortsBy !== ""

        height: header.height

        Item {
            id: hit

            anchors.verticalCenter: parent.verticalCenter
            anchors.left: cell.align === Text.AlignRight ? undefined : parent.left
            anchors.right: cell.align === Text.AlignRight ? parent.right : undefined
            anchors.leftMargin: -FileBrowserMetrics.columnHeaderPadding
            anchors.rightMargin: -FileBrowserMetrics.columnHeaderPadding
            width: labelRow.width + FileBrowserMetrics.columnHeaderPadding * 2
            height: Style.buttonHeightXXS

            HoverHandler {
                enabled: cell.sortsBy !== ""
                cursorShape: Qt.PointingHandCursor
            }

            StateLayer {
                enabled: cell.sortsBy !== ""
                stateColor: Style.surfaceText
                cornerRadius: Style.cornerRadiusS
                onClicked: header.sortRequested(cell.sortsBy)
            }

            Row {
                id: labelRow

                anchors.centerIn: parent
                spacing: Style.spacingXXS

                StyledText {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, Math.max(0, cell.width - (cell.active ? Style.iconSizeSmall + Style.spacingXXS : 0)))
                    text: FileColumns.label(cell.columnId)
                    color: cell.active ? Style.primary : Style.surfaceVariantText
                    font.pixelSize: Style.fontSizeSmall
                    font.weight: Style.fontWeightMedium
                    elide: Text.ElideRight
                }

                DIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: cell.active
                    name: header.sortDescending ? "arrow_downward" : "arrow_upward"
                    size: Style.iconSizeSmall
                    color: Style.primary
                }
            }
        }
    }

    HeaderCell {
        anchors.left: parent.left
        anchors.leftMargin: header.nameIndent
        anchors.right: optional.left
        anchors.rightMargin: FileBrowserMetrics.columnGap
        columnId: "name"
        sortsBy: "name"
    }

    Row {
        id: optional

        anchors.right: parent.right
        anchors.rightMargin: Style.spacingM
        spacing: FileBrowserMetrics.columnGap

        Repeater {
            model: header.listColumns

            HeaderCell {
                required property string modelData

                visible: header.columnIds.includes(modelData)
                width: header.columnWidth(modelData)
                columnId: modelData
                sortsBy: header.columnSortKeys[modelData] ?? FileColumns.sortKeyFor(modelData)
                align: FileColumns.specFor(modelData)?.align ?? Text.AlignLeft

                MouseArea {
                    id: grip

                    property real pressX: 0
                    property real pressWidth: 0

                    anchors.right: parent.left
                    anchors.rightMargin: (FileBrowserMetrics.columnGap - width) / 2
                    width: FileBrowserMetrics.columnGap
                    height: parent.height
                    hoverEnabled: true
                    cursorShape: Qt.SizeHorCursor
                    acceptedButtons: Qt.LeftButton
                    onPressed: mouse => {
                        pressX = mapToItem(null, mouse.x, 0).x;
                        pressWidth = parent.width;
                    }
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return;
                        const moved = mapToItem(null, mouse.x, 0).x - pressX;
                        const delta = header.LayoutMirroring.enabled ? moved : -moved;
                        header.columnResized(parent.modelData, Math.max(FileBrowserMetrics.columnMinWidth, pressWidth + delta));
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: Style.outlineWidthFocused
                        height: FileBrowserMetrics.columnGripHeight
                        radius: Style.cornerRadiusFull
                        color: grip.containsMouse || grip.pressed ? Style.primary : Style.outlineVariant
                        visible: headerHover.hovered || grip.pressed
                    }
                }
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: point => header.columnsMenuRequested(point.position.x, point.position.y)
    }
}
