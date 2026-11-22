// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick

// Infinite Desktop, as the shell sees it: where the canvas is (Hyprland writes ~/.config/rice/infinite.state when
// it settles) and, live, how it moves while you hold SUPER + ALT (rice-canvasd tells it over a small socket, one
// line per step: "hold 1", "pan DX DY", "hold 0"). The moving wallpaper (WallCanvas) and the bar and dock that get
// out of the way while you move (Settings > Desktop > Infinite Desktop) follow it.
Singleton {
    id: cv
    // infinite.state
    property bool on: false
    property int ws: -1
    property real ox: 0
    property real oy: 0
    property real zoom: 1
    // infinite.conf: the wallpaper moves with the canvas (wall=1), the bar and the dock hide while it moves (hidebar=1)
    property bool wallOn: true
    property bool hideBarOn: true
    // how much bigger than the screen the moving wallpaper is (wallzoom=1.05 ... 1.5): room to move, but also zoom
    property real wallZoom: 1.15
    // live
    property bool held: false
    property bool moved: false           // this hold moved the canvas
    property bool live: false            // the last change came from a hold (short smoothing), not from a glide
    readonly property bool here: Wm.hypr && on && Wm.focusedId === ws
    readonly property bool moving: here && held && moved
    // the bar and the dock go while you move it, and come back a moment after you let go
    property bool hideUi: false
    onMovingChanged: if (moving) { backT.stop(); hideUi = hideBarOn } else backT.restart()
    Timer { id: backT; interval: 260; onTriggered: cv.hideUi = false }

    function msg(line) {
        const p = String(line).trim().split(/\s+/)
        if (p[0] === "hold") {
            held = p[1] === "1"
            if (held) moved = false
        } else if (p[0] === "pan" && p.length >= 3) {
            const dx = Number(p[1]), dy = Number(p[2])
            if (!isFinite(dx) || !isFinite(dy) || !here) return
            live = true
            ox += dx; oy += dy
            if (held) moved = true
        }
    }

    function parseState(t) {
        const g = k => { const m = t.match(new RegExp("^" + k + "=(.*)$", "m")); return m ? m[1].trim() : "" }
        on = g("on") === "1"
        const w = parseInt(g("ws")); ws = isNaN(w) ? -1 : w
        const nz = parseFloat(g("z")); zoom = isNaN(nz) ? 1 : nz
        const nx = parseFloat(g("ox")), ny = parseFloat(g("oy"))
        // a settled place from Hyprland: glide to it (unless a hold is still going on, then it is the same place)
        if (!held) live = false
        ox = isNaN(nx) ? 0 : nx; oy = isNaN(ny) ? 0 : ny
    }
    FileView {
        path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/rice/infinite.state"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: cv.parseState(text())
        onLoadFailed: { cv.on = false }
    }
    FileView {
        path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/rice/infinite.conf"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: {
            const t = text()
            const g = (k, d) => { const m = t.match(new RegExp("^\\s*" + k + "\\s*=\\s*(\\S+)", "m")); return m ? m[1] : d }
            cv.wallOn = g("wall", "1") !== "0"
            cv.hideBarOn = g("hidebar", "1") !== "0"
            const wz = parseFloat(g("wallzoom", "1.15"))
            cv.wallZoom = isNaN(wz) ? 1.15 : Math.max(1.05, Math.min(1.5, wz))
        }
        onLoadFailed: { cv.wallOn = true; cv.hideBarOn = true; cv.wallZoom = 1.15 }
    }

    // rice-canvasd's messages: a small listener (python3) on a datagram socket, one line per message on its output.
    // (V4.74: Quickshell's SocketServer delivered nothing on some systems, so the wallpaper only moved after you let
    // go. Iridium Shell by Sarods2D feeds its parallax this way.) Moves that come in together are added up.
    readonly property string sock: (Quickshell.env("XDG_RUNTIME_DIR") || "/tmp") + "/rice-canvas.sock"
    DssProc {
        id: listen
        running: Wm.hypr
        command: ["python3", "-u", "-c", "import os, select, socket, sys\n"
            + "p = sys.argv[1]\n"
            + "try: os.unlink(p)\nexcept OSError: pass\n"
            + "s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM); s.bind(p); s.setblocking(False)\n"
            + "while True:\n"
            + "    select.select([s], [], [])\n"
            + "    dx = dy = 0\n"
            + "    while True:\n"
            + "        try: d = s.recv(256).decode(errors='replace').split()\n"
            + "        except (BlockingIOError, InterruptedError): break\n"
            + "        if len(d) == 3 and d[0] == 'pan':\n"
            + "            try: dx += float(d[1]); dy += float(d[2])\n"
            + "            except ValueError: pass\n"
            + "        elif len(d) == 2 and d[0] == 'hold':\n"
            + "            if dx or dy: print('pan %g %g' % (dx, dy)); dx = dy = 0\n"
            + "            print('hold ' + ('1' if d[1] == '1' else '0'))\n"
            + "    if dx or dy: print('pan %g %g' % (dx, dy))\n"
            + "    sys.stdout.flush()\n", cv.sock]
        stdout: DssSplit { onRead: function(l) { cv.msg(l) } }
        // it stopped (python3 missing, a crash): try again in a while
        onExited: restartT.restart()
    }
    Timer { id: restartT; interval: 5000; onTriggered: if (Wm.hypr) listen.running = true }
}
