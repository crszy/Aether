pragma ComponentBehavior: Bound
// Mix & match: every part and the shell drawing it now; click a part to pick another shell and see its options.
import QtQuick
import qs.services as Cae
import "catalog.js" as Cat

PageScroll {
    id: page

    property var win: null

    T {
        kind: "reading"
        width: parent.width
        text: "Each part of the desktop can come from a different shell."
        color: M3.inkDim
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }

    Repeater {
        model: Cat.mixParts
        SettingRow {
            id: row
            required property string modelData
            readonly property var piece: Cat.pieces[modelData]
            readonly property var drawn: Cat.shell(Cae.WinSettings[piece.mix])
            label: piece.name
            desc: piece.desc
            reserved: chip.width + 60

            Rectangle {
                id: chip
                anchors.right: arrow.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: chipRow.implicitWidth + 24
                height: 32
                radius: 16
                color: M3.secondaryContainer
                Row {
                    id: chipRow
                    anchors.centerIn: parent
                    spacing: 8
                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: row.drawn.colour
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    T {
                        text: row.drawn.name
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: M3.inkSecondaryC
                    }
                }
            }
            Icon {
                id: arrow
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron_right"
                glyph: "›"
                size: 20
                color: M3.inkDim
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: page.win && page.win.goTab(row.modelData)
            }
        }
    }
}
