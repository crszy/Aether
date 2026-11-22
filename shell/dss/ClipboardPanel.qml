// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick

// The clipboard (SUPER + V, rice-clip open): everything you copied, newest first (cliphist keeps it), texts and
// pictures. It rises from the bottom of the screen; the entries come in one after another.
// Type to search, arrows to move, Enter puts the entry back (and pastes it into the window you were in, when
// Paste is on), Delete removes it, CTRL + P pins a text to the top, Tab goes through All, Text, Pictures, Pinned,
// Esc closes. Settings > Desktop > Clipboard.
PanelWindow {
    screen: Mon.screen
    id: cb
    property bool open: false
    property real t: 0
    Behavior on t { NumberAnimation { duration: cb.open ? (Look.lite ? 240 : 520) : 200; easing.type: Easing.Linear } }
    function ease(p) { return 1 - Math.pow(1 - Math.max(0, Math.min(1, p)), 4) }
    readonly property real et: ease(t)
    onOpenChanged: {
        t = open ? 1 : 0
        if (open) { query = ""; cur = 0; filter = "all"; armed = false; listP.running = true; field.forceActiveFocus(); appear.restart() }
    }

    Trigger {
        name: "clip-open"; fresh: true
        onFired: function(o) {
            const w = o.trim().split(/\s+/)[1] || "toggle"
            cb.open = w === "open" ? true : (w === "close" ? false : !cb.open)
        }
    }

    NPal { id: pal; theme: "shell"; alpha: 0.9 }
    readonly property color fg: pal.fg
    readonly property color dim: pal.dim
    readonly property color acc: pal.primary
    readonly property color ink: pal.inkPrimary

    // ---------- the entries ----------
    property var items: []
    property var pins: []
    property bool ok: true
    property bool loaded: false
    DssProc {
        id: listP
        command: [((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-clip", "list", "80"]
        stdout: DssCollector { onStreamFinished: {
            try { const j = JSON.parse(text); cb.items = j.items || []; cb.pins = j.pins || []; cb.ok = j.ok !== false } catch (e) { cb.items = []; cb.ok = false }
            cb.loaded = true
        } }
    }
    function run(a) { DssWin.run([((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-clip"].concat(a)) }

    property string query: ""
    property string filter: "all"              // all | text | image | pin
    readonly property var filters: [{ k: "all", n: Tr.t("All"), i: "apps", g: 0xF003B }, { k: "text", n: Tr.t("Text"), i: "notes", g: 0xF09ED },
                                    { k: "image", n: Tr.t("Pictures"), i: "image", g: 0xF02E9 }, { k: "pin", n: Tr.t("Pinned"), i: "keep", g: 0xF0403 }]
    readonly property var shown: {
        const q = query.toLowerCase()
        const o = []
        if (filter === "all" || filter === "pin")
            for (let i = 0; i < pins.length; i++) if (q === "" || pins[i].toLowerCase().indexOf(q) >= 0) o.push({ kind: "pin", text: pins[i] })
        if (filter !== "pin")
            for (let i = 0; i < items.length; i++) {
                const e = items[i]
                if (filter !== "all" && e.kind !== filter) continue
                if (q !== "" && (e.kind === "image" || e.text.toLowerCase().indexOf(q) < 0)) continue
                o.push(e)
            }
        return o
    }
    property int cur: 0
    onShownChanged: if (cur >= shown.length) cur = Math.max(0, shown.length - 1)

    function pick(i) {
        const e = shown[i]
        if (!e) return
        Sfx.play("click")
        open = false
        if (e.kind === "pin") run(["copy-text", e.text])
        else run(["copy", String(e.id)])
    }
    function remove(i) {
        const e = shown[i]
        if (!e) return
        if (e.kind === "pin") { run(["unpin", e.text]); pins = pins.filter(function(p) { return p !== e.text }) }
        else { run(["delete", String(e.id)]); items = items.filter(function(x) { return x.id !== e.id }) }
    }
    function pin(i) {
        const e = shown[i]
        if (!e || e.kind !== "text") return
        run(["pin", String(e.id)])
        pinLater.restart()
    }
    Timer { id: pinLater; interval: 350; onTriggered: listP.running = true }
    // the bin: first press arms it, the second (within 3 s) forgets everything
    property bool armed: false
    Timer { id: disarm; interval: 3000; onTriggered: cb.armed = false }
    function wipe() {
        if (!armed) { armed = true; disarm.restart(); return }
        armed = false
        run(["wipe"]); items = []
    }

    // ---------- the window ----------
    visible: open || (t > 0.01 && !closeT.done)
    Timer { id: closeT; property bool done: false; interval: 600; running: !cb.open; onRunningChanged: if (running) done = false; onTriggered: done = true }
    NiriBlur { win: cb; items: [card]; radius: card.radius; on: cb.open && cb.t > 0.98 }
    color: "transparent"
    anchors { top: true; left: true; right: true; bottom: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-clipboard"
    WlrLayershell.keyboardFocus: open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    MouseArea { anchors.fill: parent; onClicked: cb.open = false }

    Shadow { anchors.fill: card; radius: card.radius; opacity: card.opacity; scale: card.scale; on: Look.shadows && Look.shadowPanels }
    Rectangle {
        id: card
        width: Math.min(560, cb.width - 32)
        // as tall as what it shows (up to 620)
        height: Math.min(cb.height - 120, 620, Math.max(340, list.y + list.contentHeight + 16))
        Behavior on height { enabled: cb.t > 0.98; NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
        x: Math.round((cb.width - width) / 2)
        // it rises from the bottom edge with a soft landing
        y: cb.height - height - 40 + (1 - cb.et) * 80
        radius: 30
        color: pal.surface
        border.width: 1; border.color: pal.line
        opacity: Math.min(1, cb.t * 2.2)
        scale: 0.92 + 0.08 * cb.et
        transformOrigin: Item.Bottom
        clip: true
        MouseArea { anchors.fill: parent }

        // ---- the head: your shape with the clipboard in it, the name, how many, the bin
        Item {
            id: head
            x: 18; y: 16; width: parent.width - 36; height: 44
            Item {
                id: badge
                width: 44; height: 44
                Blob { anchors.fill: parent; kind: Look.shape; color: pal.container
                    rotation: (1 - cb.et) * -90
                    SlowSpin { turn: 360; period: 50000; running: cb.open && !Look.lite } }
                MIcon { anchors.centerIn: parent; size: 20; name: "content_paste"; glyph: String.fromCodePoint(0xF0192); color: pal.inkContainer }
            }
            Column {
                anchors.left: badge.right; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                Text { text: Tr.t("Clipboard"); color: cb.fg; font.family: Fonts.main; font.pixelSize: 18; font.weight: Font.Bold }
                Text { text: cb.items.length + " " + Tr.t("copied") + (cb.pins.length ? "  ·  " + cb.pins.length + " " + Tr.t("pinned") : "")
                    color: cb.dim; font.family: Fonts.main; font.pixelSize: 11 }
            }
            Rectangle {
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                height: 36; radius: 18; width: binRow.implicitWidth + 24
                color: cb.armed ? "#e06c75" : (binM.containsMouse ? pal.cardHi : pal.card)
                Behavior on color { ColorAnimation { duration: 150 } }
                Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                Row { id: binRow; anchors.centerIn: parent; spacing: 6
                    MIcon { anchors.verticalCenter: parent.verticalCenter; size: 17; name: "delete_sweep"; glyph: String.fromCodePoint(0xF0A7A); color: cb.armed ? "#2a0b0e" : cb.fg }
                    Text { visible: cb.armed; anchors.verticalCenter: parent.verticalCenter; text: Tr.t("Clear all?"); color: "#2a0b0e"; font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.DemiBold } }
                MouseArea { id: binM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: cb.wipe() }
            }
        }

        // ---- the search
        Rectangle {
            id: search
            x: 16; y: head.y + head.height + 14; width: parent.width - 32; height: 46; radius: 23
            color: pal.card
            border.width: field.activeFocus ? 2 : 0; border.color: cb.acc
            MIcon { x: 16; anchors.verticalCenter: parent.verticalCenter; size: 19; name: "search"; glyph: String.fromCodePoint(0xF0349); color: cb.dim }
            TextInput {
                id: field
                x: 46; width: parent.width - 60; anchors.verticalCenter: parent.verticalCenter
                color: cb.fg; font.family: Fonts.main; font.pixelSize: 14; clip: true
                selectionColor: cb.acc; selectedTextColor: cb.ink
                focus: cb.open
                onTextChanged: { cb.query = text; cb.cur = 0 }
                Keys.onPressed: function(e) {
                    if (e.key === Qt.Key_Escape) { cb.open = false; e.accepted = true }
                    else if (e.key === Qt.Key_Down) { cb.cur = Math.min(cb.shown.length - 1, cb.cur + 1); e.accepted = true }
                    else if (e.key === Qt.Key_Up) { cb.cur = Math.max(0, cb.cur - 1); e.accepted = true }
                    else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) { cb.pick(cb.cur); e.accepted = true }
                    else if (e.key === Qt.Key_Delete) { cb.remove(cb.cur); e.accepted = true }
                    else if (e.key === Qt.Key_P && (e.modifiers & Qt.ControlModifier)) { cb.pin(cb.cur); e.accepted = true }
                    else if (e.key === Qt.Key_Tab || e.key === Qt.Key_Backtab) {
                        const ks = ["all", "text", "image", "pin"]
                        const i = ks.indexOf(cb.filter)
                        cb.filter = ks[(i + (e.key === Qt.Key_Backtab ? 3 : 1)) % 4]; cb.cur = 0; e.accepted = true
                    }
                }
            }
            Text { x: 46; anchors.verticalCenter: parent.verticalCenter; visible: field.text === ""; text: Tr.t("Search what you copied")
                color: cb.dim; font.family: Fonts.main; font.pixelSize: 14 }
        }

        // ---- the kinds
        Row {
            id: chips
            x: 16; y: search.y + search.height + 10
            spacing: 6
            Repeater {
                model: cb.filters
                delegate: Rectangle {
                    readonly property bool on: cb.filter === modelData.k
                    height: 32; radius: on ? 12 : 16; width: chR.implicitWidth + 24
                    color: on ? cb.acc : (chM.containsMouse ? pal.cardHi : pal.card)
                    Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                    Behavior on color { ColorAnimation { duration: 150 } }
                    Row { id: chR; anchors.centerIn: parent; spacing: 5
                        MIcon { anchors.verticalCenter: parent.verticalCenter; size: 15; name: modelData.i; glyph: String.fromCodePoint(modelData.g); color: parent.parent.on ? cb.ink : cb.dim }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: modelData.n; color: parent.parent.on ? cb.ink : cb.fg; font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.Medium } }
                    MouseArea { id: chM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { cb.filter = modelData.k; cb.cur = 0 } }
                }
            }
        }

        // ---- the list
        // the entries come in one after another when it opens
        property real ap: 1
        NumberAnimation { id: appear; target: card; property: "ap"; from: 0; to: 1; duration: Look.lite ? 200 : 700 }
        ListView {
            id: list
            x: 12; y: chips.y + chips.height + 12
            width: parent.width - 24; height: parent.height - y - 12
            clip: true
            spacing: 8
            model: cb.shown
            currentIndex: cb.cur
            highlightMoveDuration: 180
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 400
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool sel: index === cb.cur
                readonly property bool img: modelData.kind === "image"
                readonly property real k: Math.max(0, Math.min(1, (card.ap - Math.min(index, 8) * 0.07) / 0.45))
                width: list.width
                height: img ? 150 : Math.min(96, Math.max(54, txt.implicitHeight + 28))
                opacity: k
                transform: Translate { y: (1 - row.k) * 24 }
                Rectangle {
                    anchors.fill: parent
                    radius: row.sel ? 22 : 18
                    color: row.sel ? Qt.rgba(cb.acc.r, cb.acc.g, cb.acc.b, 0.16) : (rm.containsMouse ? pal.cardHi : pal.card)
                    border.width: row.sel ? 2 : 0; border.color: cb.acc
                    Behavior on radius { NumberAnimation { duration: 200 } }
                    Behavior on color { ColorAnimation { duration: 140 } }
                }
                MouseArea { id: rm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onEntered: cb.cur = row.index; onClicked: cb.pick(row.index) }
                // what kind it is
                Rectangle {
                    x: 12; y: row.img ? 12 : (row.height - height) / 2
                    width: 30; height: 30; radius: row.modelData.kind === "pin" ? 10 : 15
                    color: row.modelData.kind === "pin" ? cb.acc : pal.container
                    MIcon { anchors.centerIn: parent; size: 15
                        name: row.modelData.kind === "pin" ? "keep" : (row.img ? "image" : "notes")
                        glyph: String.fromCodePoint(row.modelData.kind === "pin" ? 0xF0403 : (row.img ? 0xF02E9 : 0xF09ED))
                        color: row.modelData.kind === "pin" ? cb.ink : pal.inkContainer }
                }
                Text {
                    id: txt
                    visible: !row.img
                    x: 54; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 54 - (row.sel || rm.containsMouse ? 96 : 16)
                    text: row.modelData.text || ""
                    color: cb.fg; font.family: Fonts.main; font.pixelSize: 13
                    wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                ShapeImage {
                    visible: row.img
                    x: 54; y: 10; width: parent.height - 20; height: parent.height - 20
                    kind: "square"; empty: pal.container; fallbackIcon: "image"; fallbackColor: cb.dim
                    source: row.modelData.img ? "file:///" + row.modelData.img : ""
                }
                Text { visible: row.img; anchors.right: acts.left; anchors.rightMargin: 10; anchors.bottom: parent.bottom; anchors.bottomMargin: 12
                    text: row.modelData.text || ""; color: cb.dim; font.family: Fonts.main; font.pixelSize: 10 }
                // pin and remove, on the picked or hovered entry
                Row {
                    id: acts
                    anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    opacity: row.sel || rm.containsMouse ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 140 } }
                    Act { visible: row.modelData.kind === "text"; icon: "keep"; g: 0xF0403; onHit: cb.pin(row.index) }
                    Act { icon: "close"; g: 0xF0156; onHit: cb.remove(row.index) }
                }
            }
            // nothing (yet)
            Column {
                anchors.centerIn: parent
                visible: cb.shown.length === 0
                spacing: 10
                Blob { anchors.horizontalCenter: parent.horizontalCenter; width: 64; height: 64; kind: Look.shape; color: pal.container
                    MIcon { anchors.centerIn: parent; size: 26; name: "content_paste_off"; glyph: String.fromCodePoint(0xF0192); color: pal.inkContainer } }
                Text { anchors.horizontalCenter: parent.horizontalCenter; color: cb.dim; font.family: Fonts.main; font.pixelSize: 13
                    text: !cb.ok ? Tr.t("cliphist is not installed (run the installer again)") : (!cb.loaded ? Tr.t("Loading...") : (cb.query !== "" ? Tr.t("Nothing found") : Tr.t("Copy something, it shows up here"))) }
            }
        }

    }
    Text {
        anchors.horizontalCenter: card.horizontalCenter; anchors.top: card.bottom; anchors.topMargin: 12
        opacity: cb.et * 0.8
        text: "Enter " + Tr.t("paste") + "   ·   Del " + Tr.t("remove") + "   ·   Ctrl+P " + Tr.t("pin") + "   ·   Tab " + Tr.t("kinds")
        color: "white"; style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.4); font.family: Fonts.main; font.pixelSize: 11
    }

    component Act: Rectangle {
        id: a
        property string icon: ""
        property int g: 0
        signal hit()
        width: 34; height: 34; radius: am.pressed ? 10 : 17
        color: am.containsMouse ? pal.cardHi : Qt.rgba(1, 1, 1, 0.06)
        Behavior on radius { NumberAnimation { duration: 160 } }
        MIcon { anchors.centerIn: parent; size: 16; name: a.icon; glyph: String.fromCodePoint(a.g); color: cb.fg }
        MouseArea { id: am; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: a.hit() }
    }
}
