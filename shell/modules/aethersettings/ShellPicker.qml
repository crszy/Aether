pragma ComponentBehavior: Bound
// Which shell draws one part (Mix & match): a card per shell, Off, and your own files, in DarkShadowShell's
// card style. Writes WinSettings[piece.mix], like the classic panel.
import QtQuick
import qs.services as Cae
import "catalog.js" as Cat

Grid {
    id: sp

    property var piece: null
    readonly property var model: Cat.pickerModel(piece, !piece ? [] : piece.user === "lock" ? Cae.UserExt.lockNames : piece.user === "osd" ? Cae.UserExt.osdNames : [])
    readonly property string current: piece && piece.mix ? String(Cae.WinSettings[piece.mix]) : ""

    width: parent ? parent.width : 600
    columns: Math.max(2, Math.min(5, Math.floor(width / 170)))
    columnSpacing: 8
    rowSpacing: 8

    Repeater {
        model: sp.model

        Rectangle {
            id: card
            required property var modelData
            readonly property bool chosen: sp.current === modelData.id
            width: (sp.width - sp.columnSpacing * (sp.columns - 1)) / sp.columns
            height: 80
            radius: chosen ? M3.rL : M3.rM
            opacity: modelData.offered ? 1 : 0.4
            color: chosen ? M3.secondaryContainer : (area.containsMouse && modelData.offered ? M3.surfaceHigh : M3.surfaceC)
            border.width: chosen ? 2 : 0
            border.color: modelData.colour
            scale: area.pressed && modelData.offered ? 0.97 : 1
            Behavior on color { ColorAnimation { duration: 160 } }
            Behavior on radius { NumberAnimation { duration: M3.d(260); easing.type: M3.springy } }
            Behavior on scale { NumberAnimation { duration: 110 } }

            Column {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 4
                Row {
                    width: parent.width
                    spacing: 8
                    ShellLogo {
                        size: 22
                        shell: card.modelData.id
                        icon: card.modelData.id === "off" ? "block" : "circle"
                        colour: card.modelData.colour
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    T {
                        width: parent.width - 30 - (card.chosen ? 26 : 0)
                        text: card.modelData.name
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: card.chosen ? M3.inkSecondaryC : M3.ink
                        elide: Text.ElideRight
                    }
                    Icon {
                        visible: card.chosen
                        name: "check_circle"
                        glyph: "✓"
                        size: 16
                        color: M3.primary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                T {
                    width: parent.width
                    text: card.modelData.offered ? card.modelData.blurb : "doesn't have one"
                    color: M3.inkDim
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                enabled: card.modelData.offered
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    Sfx.play("select");
                    Cae.WinSettings[sp.piece.mix] = card.modelData.id;
                }
            }
        }
    }
}
