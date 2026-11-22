// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// Toolbox > Tasks: the same list as the To-do widget on the desktop (rice-todo, ~/.config/rice/todo.json).
Item {
    id: tk
    property var pal
    // the opening wave (Tools.qml): 0 .. 1; each part comes a moment after the one above it
    property real enter: 1
    function e(i) { const p = Math.max(0, Math.min(1, (enter - i * 0.07) / 0.6)); return 1 - Math.pow(1 - p, 4) }
    property bool active: false
    property var items: []
    readonly property string tool: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-todo"
    readonly property int todo: { let n = 0; for (let i = 0; i < items.length; i++) if (!items[i].done) n++; return n }
    function focusIt() { field.input.forceActiveFocus() }
    readonly property string summary: items.length === 0 ? Tr.t("Nothing to do yet") : (todo === 0 ? Tr.t("All done") : todo + " " + Tr.t("left"))
    onActiveChanged: if (active) list.running = true
    DssProc {
        id: list
        command: [tk.tool, "list"]
        stdout: DssCollector { onStreamFinished: { try { tk.items = JSON.parse(text) } catch (e) { } } }
    }
    // a change: shown at once, then written (and read back, so the desktop widget and this never disagree for long)
    function run(args) { doer.command = [tk.tool].concat(args); doer.running = true }
    DssProc { id: doer; onExited: list.running = true }
    function add(t) {
        t = t.trim(); if (t === "") return
        items = items.concat([{ t: t, done: false }]); run(["add", t]); field.text = ""
    }
    function toggle(i) { const l = items.slice(); l[i] = { t: l[i].t, done: !l[i].done }; items = l; run(["toggle", String(i)]) }
    function remove(i) { const l = items.slice(); l.splice(i, 1); items = l; run(["remove", String(i)]) }

    ToolField { id: field; pal: tk.pal; width: parent.width; hint: Tr.t("Add a task ..."); icon: "add_task"; glyph: String.fromCodePoint(0xF0415)
    opacity: tk.e(0); transform: Translate { y: (1 - tk.e(0)) * 22 }
        onAccepted: function(t) { tk.add(t) } }
    // how much is done
    Item {
        id: prog
        opacity: tk.e(1); transform: Translate { y: (1 - tk.e(1)) * 22 }
        y: field.height + 14; width: parent.width; height: 26
        visible: tk.items.length > 0
        Text { anchors.verticalCenter: parent.verticalCenter; color: tk.pal.dim; font.family: Fonts.main; font.pixelSize: 12
            text: tk.todo === 0 ? Tr.t("All done") : tk.todo + " " + Tr.t("left") }
        WavyLine { anchors.verticalCenter: parent.verticalCenter; x: 90; width: parent.width - 90 - (clear.visible ? clear.width + 10 : 0); handle: false
            value: tk.items.length > 0 ? 1 - tk.todo / tk.items.length : 0; moving: tk.active; color: tk.pal.primary; track: tk.pal.track
            Behavior on value { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } } }
        ToolBtn { id: clear; pal: tk.pal; h: 26; kind: "plain"; label: Tr.t("Clear done"); anchors.right: parent.right
            visible: tk.todo < tk.items.length; onClicked: { tk.items = tk.items.filter(x => !x.done); tk.run(["clear"]) } }
    }
    Flickable {
        y: prog.y + prog.height + 10; width: parent.width; height: parent.height - y
        opacity: tk.e(2); transform: Translate { y: (1 - tk.e(2)) * 22 }
        contentHeight: col.height; clip: true; boundsBehavior: Flickable.StopAtBounds
        Column {
            id: col
            width: parent.width; spacing: 6
            Repeater {
                model: tk.items
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool done: modelData.done === true
                    width: col.width; height: Math.max(48, label.implicitHeight + 24)
                    radius: rm.pressed ? 12 : 18
                    Behavior on radius { NumberAnimation { duration: 160 } }
                    color: rm.containsMouse ? tk.pal.cardHi : tk.pal.card
                    Behavior on color { ColorAnimation { duration: 140 } }
                    // the box: a ring that fills and turns squarer when it is done
                    Rectangle {
                        id: box
                        x: 14; anchors.verticalCenter: parent.verticalCenter
                        width: 22; height: 22; radius: row.done ? 7 : 11
                        Behavior on radius { NumberAnimation { duration: 240; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                        color: row.done ? tk.pal.primary : "transparent"
                        Behavior on color { ColorAnimation { duration: 180 } }
                        border.width: row.done ? 0 : 2; border.color: tk.pal.dim
                        MIcon { anchors.centerIn: parent; size: 14; name: "check"; glyph: String.fromCodePoint(0xF012C); color: tk.pal.inkPrimary
                            scale: row.done ? 1 : 0
                            Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack; easing.overshoot: 2.5 } } }
                    }
                    Text {
                        id: label
                        anchors.left: box.right; anchors.leftMargin: 12; anchors.right: del.left; anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.t; wrapMode: Text.Wrap
                        font.family: Fonts.main; font.pixelSize: 14; font.strikeout: row.done
                        color: row.done ? tk.pal.dim : tk.pal.fg
                        Behavior on color { ColorAnimation { duration: 180 } }
                    }
                    MouseArea { id: rm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { Sfx.play(row.done ? "click" : "select"); tk.toggle(row.index) } }
                    Rectangle {
                        id: del
                        anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                        width: 32; height: 32; radius: 16
                        color: dm.containsMouse ? Qt.rgba(1, 0.42, 0.42, 0.2) : "transparent"
                        opacity: rm.containsMouse || dm.containsMouse ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 140 } }
                        MIcon { anchors.centerIn: parent; size: 16; name: "delete"; glyph: String.fromCodePoint(0xF01B4); color: dm.containsMouse ? "#ff8a8a" : tk.pal.dim }
                        MouseArea { id: dm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: tk.remove(row.index) }
                    }
                }
            }
        }
        // nothing yet
        Column {
            visible: tk.items.length === 0
            anchors.horizontalCenter: parent.horizontalCenter; y: 60; spacing: 10
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 72; height: 72; radius: 26; color: tk.pal.container
                MIcon { anchors.centerIn: parent; size: 32; name: "checklist"; glyph: String.fromCodePoint(0xF0135); color: tk.pal.inkContainer } }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: Tr.t("Nothing to do yet"); color: tk.pal.fg; font.family: Fonts.main; font.pixelSize: 15; font.weight: Font.DemiBold }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: Tr.t("Type a task above and press Enter"); color: tk.pal.dim; font.family: Fonts.main; font.pixelSize: 12 }
        }
    }
}
