// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// A small command file that a script writes (rice-launch, rice-wifi, rice-dashboard ...), one line per request.
// It is read inside the shell when the file changes (inotify), so nothing has to be started every few hundred
// milliseconds to look at it (that polling cost a lot of CPU, most of all on laptops). A slow re-read stays as a
// safety net. fired(line) comes once for every new line, never for the line that was there at start.
Item {
    id: tr
    property string name: ""              // the file in ~/.cache/rice
    property string path: name !== "" ? ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.cache/rice/" + name : ""
    property int every: 5000               // the safety-net re-read, in ms (inotify does the real work)
    signal fired(string line)
    signal content(string text)            // the text changed (for files that hold settings)
    // made just now because of this very line (LazyPanel.qml): the line at start fires too, when its time stamp
    // (the first word, seconds) is from the last few seconds
    property bool fresh: false
    property string prevText: "\u0000"
    property string last: ""
    property bool primed: false
    width: 0; height: 0; visible: false

    FileView {
        id: fv
        path: tr.path
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            const t = text()
            if (t !== tr.prevText) { tr.prevText = t; tr.content(t) }
            const o = t.trim()
            if (tr.primed && o !== "" && o !== tr.last) tr.fired(o)
            else if (!tr.primed && tr.fresh && o !== "") {
                const ts = parseFloat(o.split(/\s+/)[0])
                if (!isNaN(ts) && Math.abs(Date.now() / 1000 - ts) < 4) tr.fired(o)
            }
            if (o !== "") tr.last = o
            tr.primed = true
        }
        onLoadFailed: { if (tr.prevText !== "") { tr.prevText = ""; tr.content("") } tr.primed = true }
    }
    Timer { interval: tr.every; running: tr.path !== ""; repeat: true; onTriggered: fv.reload() }
}
