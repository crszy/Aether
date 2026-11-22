pragma Singleton
// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows version by dotfiles/dss-port.js: rice-stats (Linux /proc) -> FsUtils.systemStats(), same numbers
// (percent, RAM in GB) for every widget, sampled only while one shows them.
import Quickshell
import Quickshell.Windows as WinFs
import QtQuick

Singleton {
    id: stats
    property var st: ({ cpu: 0, ram: 0, gpu: 0, temp: 0, gtemp: 0, ramu: 0, ramt: 0 })
    property bool haveGpu: false
    property int users: 0
    property var last: null
    function hold() { users++ }
    function release() { users = Math.max(0, users - 1) }

    function sample() {
        const s = WinFs.FsUtils.systemStats();
        if (!s || !s.total)
            return;
        let cpu = stats.st.cpu;
        if (stats.last) {
            const busy = (s.kernel - stats.last.kernel) + (s.user - stats.last.user);
            const idle = s.idle - stats.last.idle;
            if (busy > 0)
                cpu = Math.max(0, Math.min(100, Math.round((1 - idle / busy) * 1000) / 10));
        }
        stats.last = { idle: s.idle, kernel: s.kernel, user: s.user };
        const gb = 1024 * 1024 * 1024;
        stats.st = {
            cpu: cpu, ram: Math.round((1 - s.available / s.total) * 1000) / 10, gpu: 0, temp: 0, gtemp: 0,
            ramu: Math.round((s.total - s.available) / gb * 10) / 10, ramt: Math.round(s.total / gb * 10) / 10
        };
    }

    Timer {
        interval: 1500
        repeat: true
        triggeredOnStart: true
        running: stats.users > 0
        onTriggered: stats.sample()
    }
}
