// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// A note on the desktop, saved in ~/.config/rice/note.txt. Click it to type (Esc to stop).
Item {
    id: nt
    property real bg: 1       // card background opacity (widget editor)
    property var pal: null
    readonly property bool typing: edit.activeFocus
    property bool running: true
    property bool loaded: false
    readonly property string file: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/rice/note.txt"
    readonly property color cFg: pal ? pal.fg : "#F6F5F4"
    readonly property color cFg3: pal ? pal.fg3 : "#8a8a8f"
    readonly property color cFg4: pal ? pal.fg4 : "#6a6a70"
    readonly property color cMain: pal ? pal.primary : "#F6F5F4"

    DssProc {
        command: ["bash", "-c", "cat \"$HOME/.config/rice/note.txt\" 2>/dev/null"]
        running: true
        stdout: DssCollector { onStreamFinished: { if (!nt.loaded) { edit.text = text.replace(/\n$/, ""); nt.loaded = true } } }
    }
    Timer {
        id: save
        interval: 700
        onTriggered: DssWin.run(["bash", "-c", "mkdir -p \"$HOME/.config/rice\" && printf '%s\\n' \"$1\" > \"$HOME/.config/rice/note.txt\"", "_", edit.text])
    }

    Card { anchors.fill: parent; k: nt.bg; pal: nt.pal }

    Row {
        x: 22; y: 18
        spacing: 10
        Rectangle {
            width: 30; height: 30; radius: 15
            color: nt.pal ? nt.pal.chip : Qt.rgba(1, 1, 1, 0.08)
            T { kind: "icon"; anchors.centerIn: parent; text: ""; font.pixelSize: 14; color: nt.cMain }
        }
        T { text: Tr.t("Note"); font.pixelSize: 17; font.bold: true; color: nt.cFg; anchors.verticalCenter: parent.verticalCenter }
    }

    Flickable {
        id: fl
        x: 22; y: 60
        width: parent.width - 44
        height: parent.height - 60 - 18
        contentWidth: width
        contentHeight: Math.max(height, edit.contentHeight + 8)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        TextEdit {
            id: edit
            width: fl.width
            color: nt.cFg
            selectionColor: nt.cMain
            selectedTextColor: nt.pal ? nt.pal.inkOn : "#0d0d0d"
            font.family: Fonts.family("reading")
            font.pixelSize: 14
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            persistentSelection: false
            onTextChanged: {
                if (text.length > 4000) { text = text.substring(0, 4000); return }
                if (nt.loaded) save.restart()
                if (activeFocus) Sfx.play("type")
            }
            Keys.onEscapePressed: focus = false
        }
        T {
            visible: edit.text.length === 0
            text: edit.activeFocus ? Tr.t("Type here") : Tr.t("Click to write a note")
            font.pixelSize: 14; color: nt.cFg4
        }
    }
}
