// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick

// Every key of the shell on one sheet (SUPER + /, again or Esc closes it). Type to find a key or what it does.
// The keys come from rice-keymap sheet, so the ones you changed (Settings > System > Keybinds) are shown as you
// set them, in your key color.
PanelWindow {
    id: cs
    screen: Mon.screen
    property bool open: false
    property var groups: []
    property string q: ""
    property real t: 0
    Behavior on t { NumberAnimation { duration: cs.open ? 340 : 170; easing.type: Easing.OutCubic } }
    onOpenChanged: { t = open ? 1 : 0; if (open) { q = ""; find.text = ""; load.running = true; focusT.restart() } }

    NPal { id: pal; theme: "shell"; alpha: 0.95 }

    Trigger {
        name: "keys-open"; fresh: true
        onFired: function(o) { cs.open = !cs.open; if (cs.open) Sfx.play("click") }
    }
    DssProc {
        id: load
        command: ["bash", "-c", "\"$HOME/.local/bin/rice-keymap\" sheet"]
        stdout: DssCollector { onStreamFinished: { try { cs.groups = JSON.parse(text).groups || [] } catch (e) { } } }
    }
    Timer { id: focusT; interval: 40; onTriggered: find.forceActiveFocus() }
    function hit(k) {
        if (q === "") return true
        const s = q.toLowerCase()
        return k.keys.toLowerCase().indexOf(s) >= 0 || Tr.t(k.what).toLowerCase().indexOf(s) >= 0 || k.what.toLowerCase().indexOf(s) >= 0
    }
    readonly property int hitsAll: {
        let n = 0
        for (let i = 0; i < groups.length; i++) for (let k = 0; k < groups[i].keys.length; k++) if (hit(groups[i].keys[k])) n++
        return n
    }
    // what you typed, in your key color inside the text
    function esc(s) { return s.replace(/&/g, "&amp;").replace(/</g, "&lt;") }
    function mark(s) {
        if (q === "") return esc(s)
        const at = s.toLowerCase().indexOf(q.toLowerCase())
        if (at < 0) return esc(s)
        return esc(s.slice(0, at)) + "<b><font color=\"" + pal.primary + "\">" + esc(s.slice(at, at + q.length)) + "</font></b>" + esc(s.slice(at + q.length))
    }
    // the sheet has been laid out once: from here on things glide instead of jumping
    property bool settled: false
    Timer { id: settleT; interval: 120; onTriggered: cs.settled = true }
    onGroupsChanged: { settled = false; settleT.restart() }

    visible: open || t > 0.01
    color: "transparent"
    anchors { top: true; bottom: true; left: true; right: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-keys"
    WlrLayershell.keyboardFocus: open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    NiriBlur { win: cs; items: [card]; radius: card.radius; on: cs.open && cs.t > 0.95 }

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.38 * cs.t) }
    MouseArea { anchors.fill: parent; onClicked: cs.open = false }

    // one key, drawn as a key cap
    component Cap: Rectangle {
        id: cap
        property string label: ""
        property bool yours: false
        property bool sign: false
        property bool lit: false               // it has what you typed in it
        width: sign ? ct.implicitWidth : Math.max(28, ct.implicitWidth + 16)
        height: 26; radius: lit ? 13 : 8
        Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        color: sign ? "transparent" : (lit ? pal.primary : (yours ? pal.container : pal.cardHi))
        Behavior on color { ColorAnimation { duration: 180 } }
        scale: 1
        onLitChanged: if (lit) pop.restart()
        SequentialAnimation {
            id: pop
            NumberAnimation { target: cap; property: "scale"; to: 1.14; duration: 110; easing.type: Easing.OutCubic }
            NumberAnimation { target: cap; property: "scale"; to: 1; duration: 260; easing.type: Easing.OutBack; easing.overshoot: 2.4 }
        }
        Text { id: ct; anchors.centerIn: parent; text: cap.label
            color: cap.sign ? pal.dim : (cap.lit ? pal.inkPrimary : (cap.yours ? pal.inkContainer : pal.fg))
            Behavior on color { ColorAnimation { duration: 180 } }
            font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.Medium }
    }

    Shadow { anchors.fill: card; radius: card.radius; opacity: card.opacity; on: Look.shadows }
    Rectangle {
        id: card
        readonly property int cols: cs.width >= 1500 ? 3 : (cs.width >= 1000 ? 2 : 1)
        readonly property real colW: 420
        width: Math.min(cs.width - 48, cols * colW + (cols - 1) * 16 + 48)
        height: Math.min(cs.height - 80, head.height + flow.wantH + 64 + 16)
        Behavior on height { enabled: cs.settled; NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
        anchors.horizontalCenter: parent.horizontalCenter
        y: (cs.height - height) / 2 + (1 - cs.t) * 30
        radius: 32
        color: pal.surface
        border.width: 1; border.color: pal.line
        opacity: cs.t
        scale: 0.96 + 0.04 * cs.t
        MouseArea { anchors.fill: parent }

        Item {
            id: head
            x: 24; y: 20; width: parent.width - 48; height: 48
            Row {
                anchors.verticalCenter: parent.verticalCenter; spacing: 12
                Rectangle { width: 40; height: 40; radius: 14; color: pal.container
                    MIcon { anchors.centerIn: parent; size: 21; name: "keyboard"; glyph: String.fromCodePoint(0xF030C); color: pal.inkContainer } }
                Text { anchors.verticalCenter: parent.verticalCenter; text: Tr.t("Keys"); color: pal.fg
                    font.family: Fonts.main; font.pixelSize: 20; font.weight: Font.DemiBold }
            }
            // find: the field grows when you type, and counts what it found
            Rectangle {
                id: findBox
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                readonly property bool busy: find.text !== ""
                width: Math.min(busy ? 380 : 300, parent.width * 0.6); height: 42; radius: busy ? 16 : 21
                Behavior on width { NumberAnimation { duration: 360; easing.type: Easing.OutBack; easing.overshoot: 1.4 } }
                Behavior on radius { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                color: busy ? pal.cardHi : pal.card
                Behavior on color { ColorAnimation { duration: 200 } }
                border.width: 2; border.color: find.activeFocus ? pal.primary : "transparent"
                MIcon { id: findIcon; x: 14; anchors.verticalCenter: parent.verticalCenter; size: 18; name: "search"; glyph: String.fromCodePoint(0xF0349)
                    color: findBox.busy ? pal.primary : pal.dim
                    Behavior on color { ColorAnimation { duration: 200 } } }
                // every letter: the icon nods
                SequentialAnimation {
                    id: nod
                    NumberAnimation { target: findIcon; property: "scale"; to: 1.25; duration: 90; easing.type: Easing.OutCubic }
                    NumberAnimation { target: findIcon; property: "scale"; to: 1; duration: 240; easing.type: Easing.OutBack; easing.overshoot: 3 }
                }
                TextInput {
                    id: find
                    x: 42; width: parent.width - 58 - (count.visible ? count.width + 6 : 0); anchors.verticalCenter: parent.verticalCenter
                    color: pal.fg; selectionColor: pal.primary; selectedTextColor: pal.inkPrimary
                    font.family: Fonts.main; font.pixelSize: 14; clip: true
                    cursorDelegate: Rectangle {
                        width: 2; radius: 1; color: pal.primary
                        SequentialAnimation on opacity { loops: Animation.Infinite; running: find.activeFocus
                            NumberAnimation { to: 1; duration: 80 } PauseAnimation { duration: 480 }
                            NumberAnimation { to: 0.15; duration: 260 } PauseAnimation { duration: 180 } }
                    }
                    onTextChanged: { cs.q = text.trim(); nod.restart() }
                    Keys.onEscapePressed: { if (text !== "") text = ""; else cs.open = false }
                    Text { text: Tr.t("Find a key ..."); color: pal.dim; font: find.font
                        opacity: find.text === "" ? 1 : 0; x: find.text === "" ? 0 : 12
                        Behavior on opacity { NumberAnimation { duration: 160 } }
                        Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } } }
                }
                Rectangle {
                    id: count
                    anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                    visible: scale > 0.01
                    scale: findBox.busy ? 1 : 0
                    Behavior on scale { NumberAnimation { duration: 280; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                    width: Math.max(28, countT.implicitWidth + 16); height: 26; radius: 13
                    color: cs.hitsAll > 0 ? pal.primary : pal.card
                    Behavior on color { ColorAnimation { duration: 180 } }
                    Text { id: countT; anchors.centerIn: parent; text: cs.hitsAll; color: cs.hitsAll > 0 ? pal.inkPrimary : pal.dim
                        font.family: Fonts.numbers; font.pixelSize: 12; font.weight: Font.DemiBold }
                }
            }
        }

        Flickable {
            id: body
            x: 24; y: head.y + head.height + 16
            width: parent.width - 48; height: parent.height - y - 24
            contentHeight: flow.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            // the groups, one under the other in as many columns as fit: each goes to the shortest column. Every
            // group and every key stays where it is made; typing folds away the ones that do not match and the rest
            // glide to their new places (nothing is made again, so nothing blinks)
            Item {
                id: flow
                width: body.width
                readonly property real w: (width - (card.cols - 1) * 16) / card.cols
                property real wantH: 0
                height: wantH
                Behavior on height { enabled: cs.settled; NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                function place() {
                    const hs = []
                    for (let c = 0; c < card.cols; c++) hs.push(0)
                    for (let i = 0; i < rep.count; i++) {
                        const it = rep.itemAt(i)
                        if (!it) continue
                        let c = 0
                        for (let k = 1; k < hs.length; k++) if (hs[k] < hs[c] - 1) c = k
                        it.tx = c * (w + 16); it.ty = hs[c]
                        if (it.wantH > 0) hs[c] += it.wantH + 16
                    }
                    wantH = Math.max(0, Math.max.apply(null, hs) - 16)
                }
                onWChanged: Qt.callLater(place)
                Repeater {
                    id: rep
                    model: cs.groups
                    onItemAdded: Qt.callLater(flow.place)
                    delegate: Rectangle {
                        id: grp
                        required property var modelData
                        required property int index
                        // where it should be and how tall (known at once); what is drawn glides there
                        property real tx: 0
                        property real ty: 0
                        property int hits: 0
                        property real rowsH: 0
                        readonly property real wantH: hits > 0 ? 14 + 28 + rowsH + 14 : 0
                        function count() {
                            let n = 0, h = 0
                            for (let i = 0; i < rows.count; i++) { const r = rows.itemAt(i); if (r && r.on) { n++; h += r.fullH } }
                            hits = n; rowsH = h
                        }
                        onWantHChanged: Qt.callLater(flow.place)
                        x: tx; y: ty + (1 - enter) * 26
                        Behavior on x { enabled: cs.settled; NumberAnimation { duration: 340; easing.type: Easing.OutCubic } }
                        Behavior on y { enabled: cs.settled; NumberAnimation { duration: 340; easing.type: Easing.OutCubic } }
                        width: flow.w
                        height: wantH
                        Behavior on height { enabled: cs.settled; NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                        radius: 22
                        color: pal.card
                        clip: true
                        visible: opacity > 0.01
                        opacity: (hits > 0 ? 1 : 0) * enter
                        Behavior on opacity { enabled: cs.settled; NumberAnimation { duration: 200 } }
                        // opening: the groups come up one after the other
                        property real enter: 0
                        SequentialAnimation {
                            running: cs.open
                            PropertyAction { target: grp; property: "enter"; value: 0 }
                            PauseAnimation { duration: 60 + grp.index * 45 }
                            NumberAnimation { target: grp; property: "enter"; to: 1; duration: 380; easing.type: Easing.OutCubic }
                        }
                        Column {
                            id: gcol
                            x: 16; y: 14; width: parent.width - 32
                            Row {
                                spacing: 8; height: 28
                                MIcon { anchors.verticalCenter: parent.verticalCenter; size: 17; name: grp.modelData.sym; glyph: ""; color: pal.primary }
                                Text { anchors.verticalCenter: parent.verticalCenter; text: Tr.t(grp.modelData.name); color: pal.primary
                                    font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.DemiBold }
                                // how many of its keys match what you typed
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: scale > 0.01
                                    scale: cs.q !== "" && grp.hits > 0 ? 1 : 0
                                    Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                                    width: Math.max(20, nt.implicitWidth + 12); height: 20; radius: 10; color: pal.container
                                    Text { id: nt; anchors.centerIn: parent; text: grp.hits; color: pal.inkContainer
                                        font.family: Fonts.numbers; font.pixelSize: 11; font.weight: Font.DemiBold }
                                }
                            }
                            Repeater {
                                id: rows
                                model: grp.modelData.keys
                                onItemAdded: Qt.callLater(grp.count)
                                delegate: Item {
                                    id: krow
                                    required property var modelData
                                    readonly property bool on: cs.hit(modelData)
                                    readonly property real fullH: Math.max(32, what.implicitHeight + 6) + 6
                                    onOnChanged: grp.count()
                                    onFullHChanged: Qt.callLater(grp.count)
                                    width: gcol.width
                                    height: on ? fullH : 0
                                    Behavior on height { enabled: cs.settled; NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                                    opacity: on ? 1 : 0
                                    Behavior on opacity { enabled: cs.settled; NumberAnimation { duration: krow.on ? 260 : 120 } }
                                    // a key that just matched slides in from the left a little
                                    property real slide: on ? 0 : -14
                                    Behavior on slide { enabled: cs.settled; NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                                    transform: Translate { x: krow.slide }
                                    clip: true
                                    Row {
                                        id: caps
                                        y: 6 + (krow.fullH - 6 - height) / 2
                                        spacing: 4
                                        Repeater {
                                            // "Super + F12 / F11": keys at the even places, the signs between them at the odd ones
                                            model: krow.modelData.keys.split(/\s+(\+|\/|…)\s+/)
                                            delegate: Cap { required property string modelData; required property int index
                                                label: modelData; sign: index % 2 === 1; yours: krow.modelData.yours
                                                lit: cs.q !== "" && !sign && modelData.toLowerCase().indexOf(cs.q.toLowerCase()) >= 0 }
                                        }
                                    }
                                    Text {
                                        id: what
                                        anchors.right: parent.right
                                        y: 6 + (krow.fullH - 6 - height) / 2
                                        width: parent.width - caps.width - 12
                                        textFormat: Text.StyledText
                                        text: cs.mark(Tr.t(krow.modelData.what))
                                        horizontalAlignment: Text.AlignRight; wrapMode: Text.Wrap
                                        color: pal.dim; font.family: Fonts.main; font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // nothing matches: said softly, with what you typed
            Column {
                anchors.horizontalCenter: parent.horizontalCenter; y: 28
                spacing: 8
                opacity: cs.hitsAll === 0 && cs.groups.length > 0 && cs.q !== "" ? 1 : 0
                visible: opacity > 0.01
                Behavior on opacity { NumberAnimation { duration: 220 } }
                scale: 0.92 + 0.08 * opacity
                MIcon { anchors.horizontalCenter: parent.horizontalCenter; size: 30; name: "search_off"; glyph: String.fromCodePoint(0xF0349); color: pal.dim }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: Tr.t("Nothing found"); color: pal.dim; font.family: Fonts.main; font.pixelSize: 13 }
            }
        }
    }
}
