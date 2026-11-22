pragma ComponentBehavior: Bound
// Looks: one click sets every part (the same looks as the classic panel), as DarkShadowShell-style tiles.
import QtQuick
import QtQuick.Effects
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
                height: 206
                radius: on ? M3.rXL : M3.rL
                color: on ? M3.secondaryContainer : (area.containsMouse ? M3.surfaceHigh : M3.surfaceC)
                border.width: on ? 2 : 0
                border.color: modelData.colour
                scale: area.pressed ? 0.98 : 1
                Behavior on radius { NumberAnimation { duration: M3.d(300); easing.type: M3.springy } }
                Behavior on color { ColorAnimation { duration: 150 } }
                Behavior on scale { NumberAnimation { duration: 110 } }

                Item {   // header: the shell in use (its own screenshot), rounded on top to match the card
                    id: hdr
                    readonly property string file: Cat.headerFile(card.modelData.id)
                    x: card.border.width
                    y: card.border.width
                    width: card.width - card.border.width * 2
                    height: 112
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: hdrMask
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1
                    }
                    Rectangle {
                        anchors.fill: parent
                        color: Qt.tint(M3.surfaceHigh, M3.alpha(card.modelData.colour, 0.28))
                    }
                    Image {
                        visible: hdr.file !== ""
                        anchors.fill: parent
                        source: hdr.file ? "headers/" + hdr.file : ""
                        sourceSize.width: 640
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        smooth: true
                        opacity: card.on || area.containsMouse ? 1 : 0.88
                        Behavior on opacity { NumberAnimation { duration: 150 } }
                    }
                }
                Item {
                    id: hdrMask
                    visible: false
                    width: hdr.width
                    height: hdr.height
                    layer.enabled: true
                    Rectangle {   // taller than the header so only the top corners round
                        width: parent.width
                        height: parent.height + radius
                        radius: Math.max(0, card.radius - card.border.width)
                        antialiasing: true
                    }
                }
                ShellLogo {
                    x: 16
                    y: hdr.y + hdr.height - 34
                    size: 56
                    shell: card.modelData.id
                    icon: card.modelData.icon
                    colour: card.modelData.colour
                    chosen: card.on
                }
                Rectangle {
                    visible: card.on
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    width: 30
                    height: 30
                    radius: 15
                    color: M3.alpha("#000000", 0.55)
                }
                Icon {
                    visible: card.on
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 17
                    name: "check_circle"
                    glyph: "✓"
                    size: 20
                    color: M3.primary
                }
                Column {
                    x: 18
                    y: hdr.y + hdr.height + 28
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
