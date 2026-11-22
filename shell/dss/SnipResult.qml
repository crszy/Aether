// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick

// What the screenshot tool found (Snip.qml: T, L, the Color mode): the text of a part of the screen, its translation
// or the color of a pixel, on a small card in the bottom right corner. It is on the clipboard already; here you can
// read it, select a part of it or copy it again. It goes away by itself (not while the pointer is on it).
// rice-hotcorner writes ~/.cache/rice/snip-result.json and one line to ~/.cache/rice/snip-result.
PanelWindow {
    id: sr
    screen: Mon.screen
    property bool open: false
    property string kind: "text"            // text | translation | color
    property string body: ""
    property string source: ""              // translation: the text it came from; color: rgb(...)
    property string lang: ""
    property bool showSource: false
    property bool copied: false
    property real t: 0
    Behavior on t { NumberAnimation { duration: sr.open ? 320 : 180; easing.type: Easing.OutCubic } }
    onOpenChanged: t = open ? 1 : 0

    NPal { id: pal; theme: "shell"; alpha: 0.94 }
    readonly property string home: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/"))

    Trigger {
        name: "snip-result"; fresh: true
        onFired: function(o) { data.reload() }
    }
    FileView {
        id: data
        path: sr.home + "/.cache/rice/snip-result.json"
        printErrors: false
        onLoaded: {
            let j = null
            try { j = JSON.parse(text()) } catch (e) { j = null }
            if (!j || typeof j.text !== "string" || Math.abs(Date.now() / 1000 - (j.time || 0)) > 15) return
            sr.kind = j.kind || "text"; sr.body = j.text; sr.source = j.source || ""; sr.lang = j.lang || ""
            sr.showSource = false; sr.copied = false
            sr.open = true
            away.restart()
        }
    }
    Timer { id: away; interval: sr.kind === "color" ? 6000 : 14000; onTriggered: if (hover.hovered) restart(); else sr.open = false }
    function copy(s) {
        DssWin.run(["bash", "-c", "printf %s \"$1\" | wl-copy", "_", s])
        Sfx.play("click"); copied = true; away.restart()
    }

    visible: open || t > 0.01
    color: "transparent"
    anchors { bottom: true; right: true }
    margins { bottom: 24; right: 24 }
    implicitWidth: 420 + 40
    implicitHeight: card.height + 40
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-snip-result"
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    mask: Region { item: card }
    NiriBlur { win: sr; items: [card]; radius: card.radius; on: sr.open && sr.t > 0.95 }

    component Btn: Rectangle {
        id: b
        property string label: ""
        property bool filled: false
        signal clicked()
        height: 36; radius: 18
        width: bt.implicitWidth + 28
        color: filled ? (bm.containsMouse ? Qt.lighter(pal.primary, 1.06) : pal.primary) : (bm.containsMouse ? pal.cardHi : pal.card)
        Behavior on color { ColorAnimation { duration: 140 } }
        Text { id: bt; anchors.centerIn: parent; text: b.label; color: b.filled ? pal.inkPrimary : pal.fg
            font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.DemiBold }
        MouseArea { id: bm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: b.clicked() }
    }

    Shadow { anchors.fill: card; radius: card.radius; opacity: card.opacity; on: Look.shadows }
    Rectangle {
        id: card
        x: 20; width: 420
        y: 20 + (1 - sr.t) * 24
        height: col.height + 32
        radius: 28
        color: pal.surface
        border.width: 1; border.color: pal.line
        opacity: sr.t
        HoverHandler { id: hover }

        Column {
            id: col
            x: 18; y: 16; width: parent.width - 36
            spacing: 12
            // the title: what it is, and from which language to which
            Item {
                width: parent.width; height: 36
                Rectangle {
                    id: badge
                    width: 36; height: 36; radius: sr.kind === "color" ? 18 : 12
                    color: sr.kind === "color" ? sr.body : pal.container
                    border.width: sr.kind === "color" ? 2 : 0; border.color: pal.line
                    MIcon { visible: sr.kind !== "color"; anchors.centerIn: parent; size: 19; color: pal.inkContainer
                        name: sr.kind === "translation" ? "translate" : "text_fields"
                        glyph: String.fromCodePoint(sr.kind === "translation" ? 0xF05CA : 0xF0284) }
                }
                Column {
                    anchors.left: badge.right; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                    Text { text: sr.kind === "translation" ? Tr.t("Translation") : sr.kind === "color" ? Tr.t("Color") : Tr.t("Text from the screen")
                        color: pal.fg; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Text { text: (sr.kind === "translation" && sr.lang !== "" ? sr.lang + " · " : "") + (sr.copied ? Tr.t("Copied") : Tr.t("It is on the clipboard"))
                        color: pal.dim; font.family: Fonts.main; font.pixelSize: 11 }
                }
                Rectangle {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    width: 32; height: 32; radius: 16; color: xm.containsMouse ? pal.cardHi : "transparent"
                    MIcon { anchors.centerIn: parent; size: 17; name: "close"; glyph: String.fromCodePoint(0xF0156); color: pal.dim }
                    MouseArea { id: xm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: sr.open = false }
                }
            }
            // the text (you can select a part of it with the mouse); long ones scroll
            Rectangle {
                visible: sr.kind !== "color"
                width: parent.width
                height: Math.min(220, txt.implicitHeight + 24)
                radius: 16; color: pal.card; clip: true
                Flickable {
                    id: fl
                    anchors.fill: parent; anchors.margins: 12
                    contentHeight: txt.implicitHeight; clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    TextEdit {
                        id: txt
                        width: fl.width
                        text: sr.showSource ? sr.source : sr.body
                        readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                        color: pal.fg; selectionColor: pal.primary; selectedTextColor: pal.inkPrimary
                        font.family: Fonts.main; font.pixelSize: 13
                    }
                }
            }
            // the color: its codes
            Text {
                visible: sr.kind === "color"
                text: sr.body + "    " + sr.source
                color: pal.fg; font.family: Fonts.numbers; font.pixelSize: 16; font.weight: Font.Medium
            }
            Row {
                spacing: 8
                Btn { filled: true; label: Tr.t("Copy"); onClicked: sr.copy(sr.kind !== "color" && sr.showSource ? sr.source : sr.body) }
                Btn { visible: sr.kind === "color"; label: "RGB"; onClicked: sr.copy(sr.source) }
                Btn { visible: sr.kind === "translation" && sr.source !== ""; label: sr.showSource ? Tr.t("Translation") : Tr.t("Original")
                    onClicked: { sr.showSource = !sr.showSource; away.restart() } }
            }
        }
    }
}
