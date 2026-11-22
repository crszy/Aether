// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick

import Caelestia.State
import qs.services as Cae
// Notifications shown by the shell itself (rice-cc engine shell): the server, the pop-ups and the list the
// control center shows. Their look follows Settings > Notifications (~/.config/rice/notify.json): theme,
// transparency, blur, place, width, how long they stay, corners and text size. With swaync all of this sleeps.
Scope {
    id: nfy
    property bool shellMode: Cae.WinSettings.mixNotifs === "dss"
    property bool dnd: PersistentState.doNotDisturb
    readonly property var list: srvLoader.item ? srvLoader.item.trackedNotifications.values : []
    readonly property int count: list.length
    // the lock screen shows how many wait (rice-lock notif-count)
    onCountChanged: DssWin.run(["bash", "-c", "mkdir -p \"$HOME/.cache/rice\"; echo " + count + " > \"$HOME/.cache/rice/notif-count\""])
    property var pops: []              // notifications on screen right now
    property var leaving: []           // pop-ups that are on their way out

    // ---------- Settings > Notifications ----------
    property var ns: ({})
    function v(k, d) { return ns[k] !== undefined ? ns[k] : d }
    readonly property string theme: v("colors", "shell")
    readonly property real popupAlpha: v("popup_opacity", 0.85)
    readonly property real ccAlpha: v("opacity", 0.8)
    readonly property string posX: v("pos_x", "right")
    readonly property string posY: v("pos_y", "top")
    readonly property int popW: v("popup_width", 380)
    readonly property int timeout: v("timeout", 6)
    readonly property bool sticky: v("critical_sticky", true)
    readonly property int radius: v("radius", 22) + 4
    readonly property int fontSize: v("font_size", 13)
    readonly property bool images: v("images", true)
    NPal { id: popPal; theme: nfy.theme; alpha: nfy.popupAlpha }
    readonly property var pal: popPal

    Trigger {
        path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/rice/cc.json"
        every: 3000
        onContent: function(t) {
            let j = {}
            try { j = t.trim() !== "" ? JSON.parse(t) : {} } catch (e) { j = {} }
        }
    }
    Trigger {
        path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/rice/notify.json"
        every: 4000
        onContent: function(t) { try { nfy.ns = t.trim() !== "" ? JSON.parse(t) : {} } catch (e) { } }
    }
    // the server starts a moment after the switch, so swaync has let go of the notification name
    property bool srvOn: false
    onShellModeChanged: { if (!shellMode) srvOn = false; else srvT.restart() }
    Timer { id: srvT; interval: 1500; onTriggered: if (nfy.shellMode && !clearOthers.running) clearOthers.running = true }
    // Another notification daemon may own the name first: mako, dunst or swaync start by themselves when a program
    // sends a notification before the shell is up (D-Bus starts them), most of all on niri. Then notifications came
    // as plain blue boxes. Those are closed before the shell takes the name, and taken back if one shows up later.
    DssProc {
        id: clearOthers
        command: ["bash", "-c", "for n in mako .mako-wrapped dunst .dunst-wrapped swaync .swaync-wrapped; do pkill -x \"$n\"; done; sleep 0.3"]
        onExited: { nfy.srvOn = false; Qt.callLater(function() { nfy.srvOn = nfy.shellMode }) }
    }
    DssProc {
        id: ownerProc
        // the program that owns the notification name now (no call is made to it, so nothing gets started)
        command: ["bash", "-c", "busctl --user status org.freedesktop.Notifications 2>/dev/null | sed -n 's/^Comm=//p'"]
        stdout: DssCollector {
            onStreamFinished: {
                const c = text.trim()
                if (c !== "" && !/quickshell|^qs$|^\.qs/i.test(c) && nfy.shellMode && !clearOthers.running) clearOthers.running = true
            }
        }
    }
    Timer { interval: 15000; repeat: true; running: nfy.srvOn; onTriggered: if (!ownerProc.running) ownerProc.running = true }
    Loader {
        id: srvLoader
        active: nfy.srvOn
        source: "NotifServer.qml"
    }
    Connections {
        target: srvLoader.item
        ignoreUnknownSignals: true
        function onArrived(n) { nfy.arrive(n) }
    }

    function urgent(n) { return n.urgency === 2 }
    // every pop-up is {key, n}: pop-ups are found by their number, so closing one never depends on the
    // notification object (it is gone as soon as it is dismissed)
    property int seq: 0
    property var drops: []             // keys to dismiss for good once they are gone
    signal showed(var n)               // the Dynamic bar's island shows it
    property bool islandTakes: false   // the island shows notifications, so only urgent ones pop up
    function arrive(n) {
        if (nfy.dnd && !urgent(n)) return
        nfy.showed(n)
        if (islandTakes && !urgent(n)) {
            DssWin.run(["bash", "-c", "SWAYNC_APP_NAME=\"$1\" \"$HOME/.local/bin/rice-sound\" notify", "_", n.appName || ""])
            return
        }
        const e = { key: ++nfy.seq, n: n }
        const p = pops.slice(); p.unshift(e)
        pops = p
        if (p.length > 4) leave(p[p.length - 1].key)
        try { n.closed.connect(function() { nfy.leave(e.key) }) } catch (x) { }
        DssWin.run(["bash", "-c", "SWAYNC_APP_NAME=\"$1\" \"$HOME/.local/bin/rice-sound\" notify", "_", n.appName || ""])
    }
    function find(key) { for (let i = 0; i < pops.length; i++) if (pops[i].key === key) return pops[i]; return null }
    // a pop-up first plays its way out, then leaves the list (and only then is the notification let go)
    function leave(key, andDismiss) {
        const e = find(key)
        if (!e) return
        if (andDismiss && drops.indexOf(key) < 0) drops = drops.concat([key])
        if (leaving.indexOf(key) >= 0) return
        leaving = leaving.concat([key])
        goneT.restart()
    }
    Timer {
        id: goneT; interval: 280
        onTriggered: {
            const l = nfy.leaving
            const gone = nfy.pops.filter(function(x) { return l.indexOf(x.key) >= 0 })
            nfy.pops = nfy.pops.filter(function(x) { return l.indexOf(x.key) < 0 })
            nfy.leaving = []
            const d = nfy.drops
            nfy.drops = d.filter(function(k) { return l.indexOf(k) < 0 })
            gone.forEach(function(x) { if (d.indexOf(x.key) >= 0) { try { x.n.dismiss() } catch (e) { } } })
        }
    }
    function unpop(key) { leave(key, false) }
    function dismissPop(key) { leave(key, true) }
    // from the control center's list
    function dismiss(n) {
        for (let i = 0; i < pops.length; i++) if (pops[i].n === n) { leave(pops[i].key, true); return }
        try { n.dismiss() } catch (e) { }
    }
    function clearAll() {
        const l = list.slice()
        pops.forEach(function(x) { nfy.leave(x.key) })
        l.forEach(function(n) { try { n.dismiss() } catch (e) { } })
    }
    function setDnd(on) { dnd = on; DssWin.run(["bash", "-c", "\"$HOME/.local/bin/rice-cc\" dnd " + (on ? "on" : "off")]) }

    // ---------- the pop-ups ----------
    PanelWindow {
        screen: Mon.screen
        id: popWin
        visible: nfy.shellMode && nfy.pops.length > 0
        color: "transparent"
        anchors { top: nfy.posY === "top"; bottom: nfy.posY === "bottom"; right: nfy.posX === "right"; left: nfy.posX === "left" }
        margins { top: 50; bottom: 10; left: 4; right: 4 }
        implicitWidth: nfy.popW + 40
        implicitHeight: Math.max(10, popCol.implicitHeight + 40)
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.namespace: "rice-notify-popup"
        // niri: blur behind each pop-up (Hyprland blurs this layer with its own rule)
        NiriBlur { win: popWin; radius: nfy.radius; on: popWin.visible
            items: Array.prototype.filter.call(popCol.children, function(c) { return c.popup === true && !c.out }) }
        Column {
            id: popCol
            x: 20; y: nfy.posY === "top" ? 16 : parent.height - height - 16
            width: parent.width - 40
            spacing: 10
            // the others slide softly to make room
            move: Transition { NumberAnimation { properties: "y"; duration: 460; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }
            Repeater {
                model: nfy.pops
                delegate: NotifCard {
                    required property var modelData
                    readonly property int key: modelData.key
                    width: popCol.width
                    n: modelData.n
                    nf: nfy
                    popup: true
                    out: nfy.leaving.indexOf(key) >= 0
                    onClosed: nfy.dismissPop(key)
                    onTimedOut: nfy.unpop(key)
                }
            }
        }
    }
}
