// Aether shell (AGPL-3.0-or-later). The Aether bar, island dock and dashboard settings:
// ~/.config/caelestia/aether-bar.json (Settings > Bar > Aether bar edits it; hand edits are picked up live).
// The bar's layout is Ryoku's (start / centre / end zones on a floating rail); the widget list mixes
// Ryoku's catalog with end-4, DarkShadowShell and serpantinum ideas. Widget ids: see AetherWidgets.js.
pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io
import qs.utils

Singleton {
    id: root

    readonly property string path: `${Paths.config}/aether-bar.json`
    property bool loaded: false

    // runtime only (not saved): the dashboard the clock / music widget opens
    property bool dashOpen: false
    property string dashTab: "home"
    property var dashScreen: null
    function toggleDash(tab, screen) {
        if (dashOpen && (tab === undefined || tab === dashTab)) {
            dashOpen = false;
            return;
        }
        if (tab !== undefined)
            dashTab = tab;
        if (screen !== undefined)
            dashScreen = screen;
        dashOpen = true;
    }

    // runtime only: the desktop right-click menu
    property bool menuOpen: false
    property real menuX: 0
    property real menuY: 0
    property var menuScreen: null
    function openDeskMenu(screen, x, y) {
        menuScreen = screen;
        menuX = x;
        menuY = y;
        menuOpen = true;
    }

    // runtime only: the wallpaper picker (Super+W). Each press of the key moves one wallpaper on, the motion as
    // quick as the presses; holding the key freezes every animation until it is let go.
    property bool pickerOpen: false
    property bool pickerHeld: false
    property real lastPress: 0
    property bool keyDown: false
    property bool releaseSeen: false     // the key's release reaches us (else holding is told by the repeat speed)
    signal pickerAdvance(real interval)
    function pickerPress() {
        const now = Date.now();
        const gap = now - lastPress;
        lastPress = now;
        if (!pickerOpen) {
            pickerHeld = false;
            keyDown = true;
            pickerOpen = true;
            return;
        }
        const repeat = releaseSeen ? keyDown : gap < 90;
        keyDown = true;
        holdT.restart();
        if (repeat) {
            pickerHeld = true;
            return;
        }
        pickerAdvance(gap);
    }
    function pickerTap() {          // from a click or IPC: open, or one step on
        const now = Date.now();
        const gap = now - lastPress;
        lastPress = now;
        if (!pickerOpen) {
            pickerHeld = false;
            pickerOpen = true;
        } else {
            pickerAdvance(gap);
        }
    }
    function pickerRelease() {
        releaseSeen = true;
        keyDown = false;
        pickerHeld = false;
    }
    Timer {
        id: holdT
        interval: 260         // no repeat for a while: the key is up even if its release never came
        onTriggered: {
            root.pickerHeld = false;
            if (!root.releaseSeen)
                root.keyDown = false;
        }
    }

    // bar
    property alias style: adapter.style            // "floating" (Ryoku rail) | "full" (edge to edge) | "islands" (one pill per zone, DSS)
    property alias barHeight: adapter.barHeight
    property alias gap: adapter.gap                // space around a floating bar
    property alias radius: adapter.radius
    property alias barOpacity: adapter.barOpacity
    property alias start: adapter.start
    property alias center: adapter.center
    property alias end: adapter.end
    property alias workspaces: adapter.workspaces  // how many workspace dots at least
    property alias wsNumbers: adapter.wsNumbers    // numbers instead of dots
    property alias clock24h: adapter.clock24h
    property alias clockDate: adapter.clockDate
    property alias reserve: adapter.reserve        // keep windows out from under the bar
    // dock
    property alias dock: adapter.dock
    property alias dockApps: adapter.dockApps
    property alias dockIcon: adapter.dockIcon
    property alias dockMagnify: adapter.dockMagnify
    property alias dockRunning: adapter.dockRunning
    // dashboard
    property alias lyricsBubbles: adapter.lyricsBubbles
    property alias lyricsFx: adapter.lyricsFx
    // tray
    property alias trayHidden: adapter.trayHidden    // tray icons tucked into the overflow (right-click one to toggle)
    property alias trayCompact: adapter.trayCompact  // show only an arrow; the icons open in a small panel
    function isTrayHidden(id) {
        return trayHidden.indexOf(id) >= 0;
    }
    function toggleTrayHidden(id) {
        adapter.trayHidden = isTrayHidden(id) ? trayHidden.filter(a => a !== id) : trayHidden.concat([id]);
    }

    function zone(name) {
        return name === "start" ? start : name === "center" ? center : end;
    }
    function setZone(name, list) {
        if (name === "start")
            adapter.start = list;
        else if (name === "center")
            adapter.center = list;
        else
            adapter.end = list;
    }
    function has(id) {
        return start.indexOf(id) >= 0 || center.indexOf(id) >= 0 || end.indexOf(id) >= 0;
    }
    function addWidget(id, zoneName) {
        if (has(id) && id !== "spacer")
            return;
        setZone(zoneName, zone(zoneName).concat([id]));
    }
    function removeAt(zoneName, index) {
        const l = zone(zoneName).slice();
        l.splice(index, 1);
        setZone(zoneName, l);
    }
    function moveAt(zoneName, index, step) {
        const l = zone(zoneName).slice();
        const j = index + step;
        if (j < 0 || j >= l.length)
            return;
        const t = l[index];
        l[index] = l[j];
        l[j] = t;
        setZone(zoneName, l);
    }
    function isPinned(id) {
        return dockApps.indexOf(id) >= 0;
    }
    function togglePin(id) {
        adapter.dockApps = isPinned(id) ? dockApps.filter(a => a !== id) : dockApps.concat([id]);
    }
    function resetLayout() {
        adapter.start = ["logo", "workspaces", "window"];
        adapter.center = ["clock"];
        adapter.end = ["music", "sysmon", "tray", "volume", "network", "battery", "notifs", "power"];
    }

    FileView {
        id: file
        path: root.path
        watchChanges: true
        onFileChanged: reload()
        onLoaded: root.loaded = true
        onLoadFailed: err => {
            if (err === FileViewError.FileNotFound)
                writeAdapter();
            root.loaded = true;
        }
        onAdapterUpdated: writeAdapter()

        JsonAdapter {
            id: adapter
            property string style: "floating"
            property int barHeight: 38
            property int gap: 8
            property int radius: 19
            property real barOpacity: 0.86
            property list<string> start: ["logo", "workspaces", "window"]
            property list<string> center: ["clock"]
            property list<string> end: ["music", "sysmon", "tray", "volume", "network", "battery", "notifs", "power"]
            property int workspaces: 5
            property bool wsNumbers: false
            property bool clock24h: true
            property bool clockDate: true
            property bool reserve: true
            property bool dock: true
            property list<string> dockApps: []
            property int dockIcon: 44
            property bool dockMagnify: true
            property bool dockRunning: true
            property bool lyricsBubbles: true
            property bool lyricsFx: true
            property list<string> trayHidden: []
            property bool trayCompact: false
        }
    }
}
