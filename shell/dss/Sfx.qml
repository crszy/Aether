// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick

// Interface sounds (Settings > Sfx & sounds). Sfx.play("click") anywhere in this shell.
// Plays with QtMultimedia when it is installed (instant), else with pw-play / paplay.
// Only DarkShadow's Shell's own parts use it, never other apps.
Singleton {
    id: sfx
    property bool enabled: true
    // nothing plays until your settings are read (else a sound could slip out while they are off)
    property bool loaded: false
    property int volume: 45
    property string pack: "tap"
    property var opt: ({ click: true, "switch": true, slider: true, typing: true, music: true })
    readonly property string dir: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/share/rice/sounds/sfx/"
    property var bank: null
    property var lastAt: ({})
    readonly property var kinds: ({ click: "click", select: "click", done: "click", "switch-on": "switch", "switch-off": "switch",
                                    slide: "slider", type: "typing",
                                    "page-next": "click", "page-prev": "click", add: "click", remove: "click" })
    // the same sound again this soon is skipped (typing, sliding)
    readonly property var gaps: ({ slide: 45, type: 30, click: 35 })

    function file(ev) { return ev.indexOf("intro/") === 0 ? dir + ev + ".wav" : dir + pack + "/" + ev + ".wav" }
    function allowed(ev) {
        if (!loaded || !enabled || volume <= 0) return false
        if (ev.indexOf("intro/") === 0) return true          // the welcome intro and the tour: only the main switch
        return opt[kinds[ev] || "click"] !== false
    }
    // outside: play in its own process, so it is heard even if this window closes right away
    function play(ev, outside) {
        if (!allowed(ev)) return
        const now = Date.now(), g = gaps[ev] || 0
        if (g > 0 && lastAt[ev] && now - lastAt[ev] < g) return
        lastAt[ev] = now
        if (bank && !outside && bank.play(ev)) return
        const v = (volume / 100).toFixed(2)
        DssWin.run(["bash", "-c",
            "f=\"$1\"; v=\"$2\"; [ -f \"$f\" ] || exit 0; if command -v pw-play >/dev/null; then exec pw-play --media-role Event --volume \"$v\" \"$f\"; " +
            "elif command -v paplay >/dev/null; then exec paplay --volume \"$(awk -v v=\"$v\" 'BEGIN{print int(v*65536)}')\" \"$f\"; fi",
            "_", file(ev), v])
    }
    function music(on) {
        DssWin.run(["bash", "-c", "\"$HOME/.local/bin/rice-sfx\" music " + (on ? "start" : "stop")])
    }

    function applyCfg(t) {
        try {
            const j = JSON.parse(t)
            if (typeof j.enabled === "boolean") enabled = j.enabled
            if (j.volume !== undefined) volume = Math.max(0, Math.min(100, Number(j.volume)))
            if (j.pack) pack = j.pack
            const o = {}
            for (const k of ["click", "switch", "slider", "typing", "music"]) o[k] = j[k] !== false
            opt = o
        } catch (e) { }
        loaded = true
    }
    // the file is watched (inotify), not read by a bash process every 1.5 s; the slow re-read catches a late create
    FileView {
        id: cfgFile
        path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/rice/sfx.json"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: sfx.applyCfg(text())
        onLoadFailed: sfx.applyCfg("{}")
    }
    Timer { interval: 30000; running: true; repeat: true; onTriggered: cfgFile.reload() }

    Component.onCompleted: {
        // QtMultimedia is optional: without it the sounds still play (a little later) through pw-play
        const c = Qt.createComponent("SfxBank.qml")
        if (c.status === Component.Ready) bank = c.createObject(sfx, { sfx: sfx })
    }
}
