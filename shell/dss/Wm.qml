// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
pragma Singleton
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import QtQuick

// The window manager the shell runs in: Hyprland or niri. The bar, the island and the game bar ask this for the
// workspaces and the focused window, so they work the same on both.
// Hyprland: Quickshell's own Hyprland module. niri: `niri msg --json event-stream` (the workspaces of each monitor,
// the windows, the focused one), kept up to date as niri sends changes.
Singleton {
    id: wm
    readonly property bool niri: !!Quickshell.env("NIRI_SOCKET")
    readonly property bool hypr: !niri
    readonly property string name: niri ? "niri" : "Hyprland"

    // ---------- niri: what the event stream says ----------
    property var nWs: []                 // [{id, idx, name, output, is_active, is_focused, active_window_id}]
    property var nWin: ({})              // id -> {id, title, app_id, workspace_id, is_focused, is_floating}
    property var nFocusWin: null         // id of the focused window
    property bool overview: false
    property int tick: 0                 // changes whenever something changed (bindings follow it)
    property int titleTick: 0            // the focused window's title changed

    DssProc {
        id: stream
        running: wm.niri
        command: ["niri", "msg", "--json", "event-stream"]
        stdout: DssSplit {
            onRead: function(line) {
                let ev
                try { ev = JSON.parse(line) } catch (e) { return }
                wm.handle(ev)
            }
        }
        // niri restarted or the stream stopped: start it again
        onExited: restart.restart()
    }
    Timer { id: restart; interval: 1500; onTriggered: if (wm.niri) stream.running = true }

    function handle(ev) {
        if (ev.WorkspacesChanged) {
            nWs = ev.WorkspacesChanged.workspaces
        } else if (ev.WorkspaceActivated) {
            const id = ev.WorkspaceActivated.id, foc = ev.WorkspaceActivated.focused
            const t = nWs.find(w => w.id === id)
            if (!t) return
            nWs = nWs.map(w => {
                const c = Object.assign({}, w)
                if (w.output === t.output) c.is_active = w.id === id
                if (foc) c.is_focused = w.id === id
                return c
            })
        } else if (ev.WorkspaceActiveWindowChanged) {
            const e = ev.WorkspaceActiveWindowChanged
            nWs = nWs.map(w => w.id === e.workspace_id ? Object.assign({}, w, { active_window_id: e.active_window_id }) : w)
        } else if (ev.WindowsChanged) {
            const m = {}
            let f = null
            for (const w of ev.WindowsChanged.windows) { m[w.id] = w; if (w.is_focused) f = w.id }
            nWin = m
            nFocusWin = f
        } else if (ev.WindowOpenedOrChanged) {
            const w = ev.WindowOpenedOrChanged.window
            const old = nWin[w.id]
            // only the title changed (terminals and browsers do that all the time): no need to wake the whole bar
            if (old && old.workspace_id === w.workspace_id && old.is_focused === w.is_focused && old.is_floating === w.is_floating) {
                old.title = w.title; old.app_id = w.app_id
                if (w.is_focused) titleTick++
                return
            }
            const m = Object.assign({}, nWin)
            if (w.is_focused) for (const k in m) if (m[k].is_focused) m[k] = Object.assign({}, m[k], { is_focused: false })
            m[w.id] = w
            nWin = m
            if (w.is_focused) nFocusWin = w.id
        } else if (ev.WindowClosed) {
            const m = Object.assign({}, nWin)
            delete m[ev.WindowClosed.id]
            nWin = m
            if (nFocusWin === ev.WindowClosed.id) nFocusWin = null
        } else if (ev.WindowFocusChanged) {
            nFocusWin = ev.WindowFocusChanged.id
        } else if (ev.OverviewOpenedOrClosed) {
            overview = ev.OverviewOpenedOrClosed.is_open
        } else return
        // niri sends a few events for one switch (the workspace, its window, the focus): the bar and the island
        // look at them once, after the last one, instead of three times in a row
        if (!pending) { pending = true; Qt.callLater(wm.bump) }
    }
    property bool pending: false
    function bump() { pending = false; tick++ }

    // the monitor the user works on (niri: the one with the focused workspace)
    readonly property string focusedOutput: {
        tick
        const f = nWs.find(w => w.is_focused)
        return f ? (f.output || "") : ""
    }
    function wsOn(out) {
        const o = out || focusedOutput
        return nWs.filter(w => w.output === o).sort((a, b) => a.idx - b.idx)
    }

    // ---------- what the shell asks (the same on both) ----------
    // the number of the active workspace (niri: its place on the focused monitor, 1 = the top one)
    readonly property int focusedId: {
        if (niri) {
            tick
            const f = nWs.find(w => w.is_focused) || nWs.find(w => w.is_active && w.output === focusedOutput)
            return f ? f.idx : 1
        }
        return Hyprland.focusedWorkspace ? Hyprland.focusedWorkspace.id : 1
    }
    // a workspace with windows on it
    function occupied(n) {
        if (niri) {
            const w = wsOn("").find(x => x.idx === n)
            if (!w) return false
            for (const k in nWin) if (nWin[k].workspace_id === w.id) return true
            return false
        }
        const l = Hyprland.workspaces.values
        for (let i = 0; i < l.length; i++) if (l[i].id === n && l[i].toplevels && l[i].toplevels.values.length > 0) return true
        return false
    }
    // the highest workspace number in use (at least `min`)
    function maxWs(min) {
        let m = min || 1
        if (niri) {
            // niri always keeps one empty workspace at the end: it is not counted
            const l = wsOn("")
            for (const w of l) if ((occupied(w.idx) || w.is_active) && w.idx > m) m = w.idx
            return Math.min(m, 20)
        }
        const l = Hyprland.workspaces.values
        for (let i = 0; i < l.length; i++) if (l[i].id > m && l[i].id <= 20) m = l[i].id
        return m
    }
    // the focused window: its title and app
    readonly property var focusedWindow: {
        tick; titleTick
        if (niri) {
            const w = nFocusWin !== null ? nWin[nFocusWin] : null
            return w ? { title: w.title || "", appId: w.app_id || "", floating: w.is_floating === true } : null
        }
        const t = Hyprland.activeToplevel
        return t ? { title: t.title || "", appId: t.lastIpcObject ? (t.lastIpcObject.class || "") : "", floating: false } : null
    }
    readonly property string activeTitle: focusedWindow ? focusedWindow.title : ""
    readonly property string activeApp: focusedWindow ? focusedWindow.appId : ""

    // go to a workspace: a number, or "e+1" / "e-1" for the next / previous one
    // Next / previous: at most one step every 0.22 s. A fast wheel (or a high-resolution one) sent a step for every
    // event, and on a slow computer the steps queued up and played out after you stopped: the workspaces flickered.
    property real lastGo: 0
    function goWs(n) {
        const s = String(n)
        if (s.indexOf("e") === 0) {
            const now = Date.now()
            if (now - lastGo < 220) return
            lastGo = now
        }
        DssWin.run(["bash", "-c", "\"$HOME/.local/bin/ws-go\" \"$1\"", "_", s])
    }
}
