// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick

// One cava for the whole shell. The visualizer on the bar and the visualizer widget used to start one cava each
// (and read and split every line of it each, 30 times a second); now they share this one. It runs while anything
// listens (CavaUser) and stops when nothing does. bands: the 64 levels (0..100) of the last frame.
Singleton {
    id: feed
    property int users: 0
    property var bands: []
    signal frame()
    DssProc {
        command: [((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-cava"]
        running: feed.users > 0
        stdout: DssSplit {
            onRead: function(line) {
                const s = line.split(";"), p = []
                for (let i = 0; i < s.length; i++) if (s[i].length > 0) p.push(+s[i])
                if (p.length === 0) return
                feed.bands = p
                feed.frame()
            }
        }
        onRunningChanged: if (!running) feed.bands = []
    }
}
