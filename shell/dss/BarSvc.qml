// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick

// What the bar asks the system: the Wi-Fi / Bluetooth / Do not disturb switches (rice-services) and the weather, for
// every bar at once. Since V4.55 "Bar on every screen" makes a bar for each screen, and each copy used to start its
// own rice-services every 8 s and its own rice-weather.
Singleton {
    id: svc
    property bool shown: false          // the top bar is on screen
    property bool sideShown: false      // the side bar (VBar) is on screen; only while one of them is, anything is asked
    property var state: ({})            // rice-services state
    property var wx: null               // rice-weather json
    function refresh() { if (!svcProc.running) svcProc.running = true }
    // after a click: rice-cc writes the new state first (it used to be a 700 ms timer in each bar)
    function refreshSoon() { soon.restart() }
    function refreshWx() { if (!wxProc.running) wxProc.running = true }
    DssProc {
        id: svcProc
        command: ["bash", "-c", "\"$HOME/.local/bin/rice-services\" state 2>/dev/null"]
        stdout: DssCollector { onStreamFinished: { try { svc.state = JSON.parse(text) } catch (e) { } } }
    }
    Timer { interval: 8000; running: svc.shown || svc.sideShown; repeat: true; triggeredOnStart: true; onTriggered: svc.refresh() }
    Timer { id: soon; interval: 700; onTriggered: svc.refresh() }
    DssProc {
        id: wxProc
        command: [((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-weather", "json"]
        stdout: DssCollector { onStreamFinished: { try { const j = JSON.parse(text.trim().split("\n").pop()); if (j && j.temp !== undefined) svc.wx = j } catch (e) { } } }
    }
}
