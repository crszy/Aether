// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// Toolbox > Note: the same note as the Note widget on the desktop (~/.config/rice/note.txt), saved as you type.
Item {
    id: nt
    property var pal
    // the opening wave (Tools.qml): 0 .. 1; each part comes a moment after the one above it
    property real enter: 1
    function e(i) { const p = Math.max(0, Math.min(1, (enter - i * 0.07) / 0.6)); return 1 - Math.pow(1 - p, 4) }
    property bool active: false
    property bool loaded: false
    property bool saved: true
    function focusIt() { edit.forceActiveFocus() }
    readonly property string summary: Tr.t("Saved as you type")
    onActiveChanged: if (active && !edit.activeFocus) { loaded = false; load.running = true }
    DssProc {
        id: load
        command: ["bash", "-c", "cat \"$HOME/.config/rice/note.txt\" 2>/dev/null"]
        stdout: DssCollector { onStreamFinished: { if (!nt.loaded) { edit.text = text.replace(/\n$/, ""); nt.loaded = true; nt.saved = true } } }
    }
    Timer {
        id: save
        interval: 600
        onTriggered: { DssWin.run(["bash", "-c", "mkdir -p \"$HOME/.config/rice\" && printf '%s\\n' \"$1\" > \"$HOME/.config/rice/note.txt\"", "_", edit.text]); nt.saved = true }
    }
    Rectangle {
        width: parent.width; height: parent.height - 34
        opacity: nt.e(0); transform: Translate { y: (1 - nt.e(0)) * 22 }
        radius: edit.activeFocus ? 20 : 26
        Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        color: nt.pal.card
        border.width: 2; border.color: edit.activeFocus ? nt.pal.primary : "transparent"
        Behavior on border.color { ColorAnimation { duration: 180 } }
        Flickable {
            id: fl
            anchors.fill: parent; anchors.margins: 16
            contentHeight: edit.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
            function follow(r) { if (r.y < contentY) contentY = r.y; else if (r.y + r.height > contentY + height) contentY = r.y + r.height - height }
            TextEdit {
                id: edit
                width: fl.width
                height: Math.max(fl.height, implicitHeight)
                wrapMode: TextEdit.Wrap; selectByMouse: true
                color: nt.pal.fg; selectionColor: nt.pal.primary; selectedTextColor: nt.pal.inkPrimary
                font.family: Fonts.main; font.pixelSize: 14
                onTextChanged: if (nt.loaded) { nt.saved = false; save.restart() }
                onCursorRectangleChanged: fl.follow(cursorRectangle)
                Text { visible: edit.text === ""; text: Tr.t("Write anything here ..."); color: nt.pal.dim; font: edit.font }
            }
        }
    }
    Row {
        anchors.bottom: parent.bottom; anchors.bottomMargin: 4; x: 6; spacing: 8
        opacity: nt.e(1); transform: Translate { y: (1 - nt.e(1)) * 22 }
        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 8; height: 8; radius: 4; color: nt.saved ? nt.pal.primary : nt.pal.dim
            Behavior on color { ColorAnimation { duration: 200 } } }
        Text { color: nt.pal.dim; font.family: Fonts.main; font.pixelSize: 12
            text: (nt.saved ? Tr.t("Saved") : Tr.t("Saving ...")) + "  ·  " + edit.text.trim().split(/\s+/).filter(s => s !== "").length + " " + Tr.t("words") }
    }
}
