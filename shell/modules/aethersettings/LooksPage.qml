pragma ComponentBehavior: Bound
// Looks: one click sets every part (the same looks as the classic panel), as DarkShadowShell-style tiles.
import QtQuick
import Quickshell
import qs.services as Cae
import "catalog.js" as Cat

PageScroll {
    id: page

    function isActive(p) {
        return Cat.lookActive(p, Cae.WinSettings);
    }
    function apply(p) {
        // a distro look takes the whole desktop; any other look leaves distro mode
        Cae.WinSettings.distro = p.distro || "";
        for (const k of Object.keys(p.mix))
            Cae.WinSettings[k] = p.mix[k];
        for (const k of Object.keys(p.also || {}))
            Cae.WinSettings[k] = p.also[k];
        if (p.scheme)
            Quickshell.execDetached(["caelestia", "scheme", "set", "-n", p.scheme]);
        Sfx.play("done");
    }

    T {
        kind: "reading"
        width: parent.width
        text: "A look picks the shell for every part at once; a distro look (Debian) is a whole Linux desktop. Change single parts afterwards in Mix & match or on their own pages."
        color: M3.inkDim
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }

    Grid {
        id: grid
        width: parent.width
        columns: Math.max(1, Math.min(3, Math.floor(width / 260)))
        columnSpacing: 12
        rowSpacing: 12
        topPadding: 8

        Repeater {
            model: Cat.looks
            Rectangle {
                id: card
                required property var modelData
                readonly property bool on: page.isActive(modelData)
                width: (grid.width - grid.columnSpacing * (grid.columns - 1)) / grid.columns
                height: 150
                radius: on ? M3.rXL : M3.rL
                color: on ? M3.secondaryContainer : (area.containsMouse ? M3.surfaceHigh : M3.surfaceC)
                border.width: on ? 2 : 0
                border.color: modelData.colour
                scale: area.pressed ? 0.98 : 1
                Behavior on radius { NumberAnimation { duration: M3.d(300); easing.type: M3.springy } }
                Behavior on color { ColorAnimation { duration: 150 } }
                Behavior on scale { NumberAnimation { duration: 110 } }

                ShellLogo {
                    x: 18
                    y: 18
                    size: 48
                    shell: card.modelData.id
                    icon: card.modelData.icon
                    colour: card.modelData.colour
                    chosen: card.on
                }
                Icon {
                    visible: card.on
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 18
                    name: "check_circle"
                    glyph: "✓"
                    size: 20
                    color: M3.primary
                }
                Column {
                    x: 18
                    y: 78
                    width: parent.width - 36
                    spacing: 3
                    T {
                        kind: "title"
                        text: card.modelData.name
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        color: card.on ? M3.inkSecondaryC : M3.ink
                    }
                    T {
                        kind: "reading"
                        width: parent.width
                        text: card.modelData.blurb
                        color: M3.inkDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.apply(card.modelData)
                }
            }
        }
    }
}
