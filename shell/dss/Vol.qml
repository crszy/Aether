// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick

// The volume curve (Settings > Sound > Volume, rice-sound): every slider and wheel of the shell shows and sets the
// volume through it, so 20 % is still easy to hear (gentle) and the top can be louder than 100 % (max).
// PipeWire's (and Quickshell's) volume v is the cube root of the loudness; the slider value d gives the loudness
// max^3 * d^p:  v = max * d^(p/3),  d = (v / max)^(3/p).  p: 3 natural (as before V4.84), 1.8 gentle
Singleton {
    id: vo
    property string curve: "gentle"
    property real max: 1
    readonly property real p: curve === "natural" ? 3 : 1.8
    function ui(v) { return Math.max(0, Math.min(1, Math.pow(Math.max(0, v) / max, 3 / p))) }
    function pw(d) { return max * Math.pow(Math.max(0, Math.min(1, d)), p / 3) }
    // a node's volume on the slider, and setting it from the slider
    function of(n) { return n && n.audio ? ui(n.audio.volume) : 0 }
    function set(n, d) { if (n && n.audio) { n.audio.muted = false; n.audio.volume = pw(d) } }
    function nudge(n, dd) { if (n && n.audio) { if (dd > 0) n.audio.muted = false; n.audio.volume = pw(ui(n.audio.volume) + dd) } }
    FileView {
        path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/rice/sound.json"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            let j = {}
            try { j = JSON.parse(text()) } catch (e) { j = {} }
            vo.curve = j.curve === "natural" ? "natural" : "gentle"
            vo.max = [100, 125, 150].indexOf(Number(j.max)) >= 0 ? Number(j.max) / 100 : 1
        }
        onLoadFailed: { vo.curve = "gentle"; vo.max = 1 }
    }
}
