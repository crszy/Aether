// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// To-do list on the desktop. The tasks live in ~/.config/rice/todo.json (rice-todo): click a circle to tick a task,
// type at the bottom and press Enter to add one. Needs a click first to get the keyboard.
Item {
    id: td
    property real bg: 1       // card background opacity (widget editor)
    property var pal: null
    property bool running: true
    readonly property bool typing: input.activeFocus
    property var items: []
    property bool quiet: false   // right after a change here: do not let an older file overwrite it
    readonly property string tool: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-todo"
    readonly property color cFg: pal ? pal.fg : "#F6F5F4"
    readonly property color cFg3: pal ? pal.fg3 : "#8a8a8f"
    readonly property color cFg4: pal ? pal.fg4 : "#6a6a70"
    readonly property color cMain: pal ? pal.primary : "#F6F5F4"
    readonly property int remaining: { let n = 0; for (let i = 0; i < items.length; i++) if (!items[i].done) n++; return n }

    function act(args) {
        DssWin.run([td.tool].concat(args.map(function(a) { return String(a) })))
        quiet = true
        settle.restart()
    }
    function toggle(i) {
        const n = items.slice(); n[i] = { t: n[i].t, done: !n[i].done }; items = n
        act(["toggle", i])
    }
    function remove(i) {
        const n = items.slice(); n.splice(i, 1); items = n
        act(["remove", i])
    }
    function add(t) {
        t = t.trim()
        if (t.length === 0) return
        items = items.concat([{ t: t, done: false }])
        act(["add", t])
    }

    DssProc {
        id: proc
        command: [td.tool, "list"]
        stdout: DssCollector {
            onStreamFinished: {
                if (td.quiet) return
                try {
                    const j = JSON.parse(text)
                    if (JSON.stringify(j) !== JSON.stringify(td.items)) td.items = j
                } catch (e) { }
            }
        }
    }
    Timer { interval: 2500; running: td.running && td.visible; repeat: true; triggeredOnStart: true; onTriggered: if (!proc.running) proc.running = true }
    Timer { id: settle; interval: 1200; onTriggered: { td.quiet = false; proc.running = true } }

    Card { anchors.fill: parent; k: td.bg; pal: td.pal }

    // header
    Row {
        id: head
        x: 22; y: 18
        spacing: 10
        Rectangle {
            width: 30; height: 30; radius: 15
            color: td.pal ? td.pal.chip : Qt.rgba(1, 1, 1, 0.08)
            T { kind: "icon"; anchors.centerIn: parent; text: "\uf14a"; font.pixelSize: 14; color: td.cMain }
        }
        T { text: Tr.t("To-do"); font.pixelSize: 17; font.bold: true; color: td.cFg; anchors.verticalCenter: parent.verticalCenter }
    }
    T {
        anchors.right: parent.right; anchors.rightMargin: 22
        y: 18 + (30 - height) / 2
        text: td.remaining === 0 ? (td.items.length ? Tr.t("All done") : "") : td.remaining + Tr.t(" left")
        font.pixelSize: 12; color: td.cFg3
    }

    // the tasks
    Flickable {
        id: fl
        x: 14; y: 60
        width: parent.width - 28
        height: parent.height - 60 - 62
        contentWidth: width
        contentHeight: list.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: list
            width: fl.width
            Repeater {
                model: td.items
                delegate: Item {
                    id: row
                    width: list.width
                    height: 34
                    readonly property bool done: modelData.done === true
                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 1; anchors.bottomMargin: 1
                        radius: 10
                        color: rowHover.containsMouse ? (td.pal ? td.pal.chip : Qt.rgba(1, 1, 1, 0.06)) : "transparent"
                    }
                    Rectangle {
                        id: box
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 20; height: 20; radius: 10
                        color: row.done ? td.cMain : "transparent"
                        border.width: row.done ? 0 : 2
                        border.color: td.cFg3
                        Behavior on color { ColorAnimation { duration: 160 } }
                        T { visible: row.done; kind: "icon"; anchors.centerIn: parent; text: ""; font.pixelSize: 10; color: td.pal ? td.pal.inkOn : "#0d0d0d" }
                    }
                    T {
                        anchors.left: box.right; anchors.leftMargin: 10
                        anchors.right: del.left; anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.t
                        font.pixelSize: 13
                        font.strikeout: row.done
                        color: row.done ? td.cFg4 : td.cFg
                        elide: Text.ElideRight
                    }
                    T {
                        id: del
                        anchors.right: parent.right; anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        kind: "icon"; text: ""; font.pixelSize: 11
                        color: td.cFg3
                        opacity: rowHover.containsMouse ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                    }
                    MouseArea {
                        id: rowHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        property bool sfxOwn: true
                        onClicked: function(m) {
                            if (m.x > row.width - 34) { Sfx.play("remove"); td.remove(index) }
                            else { Sfx.play(row.done ? "switch-off" : "switch-on"); td.toggle(index) }
                        }
                    }
                }
            }
        }
        T {
            visible: td.items.length === 0
            x: 10; y: 6
            width: parent.width - 20
            text: Tr.t("Nothing to do. Add a task below.")
            font.pixelSize: 12; color: td.cFg3; wrapMode: Text.WordWrap
        }
    }

    // add a task
    Rectangle {
        x: 14
        y: parent.height - 14 - 38
        width: parent.width - 28
        height: 38
        radius: 12
        color: td.pal ? td.pal.chip : Qt.rgba(1, 1, 1, 0.08)
        border.width: input.activeFocus ? 1.5 : 0
        border.color: td.cMain
        T { kind: "icon"; x: 12; anchors.verticalCenter: parent.verticalCenter; text: ""; font.pixelSize: 11; color: td.cFg3 }
        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 34; anchors.rightMargin: 12
            verticalAlignment: TextInput.AlignVCenter
            color: td.cFg
            selectionColor: td.cMain
            selectedTextColor: td.pal ? td.pal.inkOn : "#0d0d0d"
            font.family: Fonts.family("reading")
            font.pixelSize: 13
            clip: true
            maximumLength: 200
            onAccepted: { if (text.trim().length > 0) Sfx.play("add"); td.add(text); text = "" }
            onTextChanged: if (activeFocus) Sfx.play("type")
            Keys.onEscapePressed: focus = false
        }
        T {
            visible: input.text.length === 0
            x: 34; anchors.verticalCenter: parent.verticalCenter
            text: input.activeFocus ? Tr.t("Type a task, press Enter") : Tr.t("Click to add a task")
            font.pixelSize: 13; color: td.cFg4
        }
    }
}
