// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
// On Windows: serpantinum-core (Linux /proc) is replaced by FsUtils.systemStats() for CPU and RAM, `netstat -e`
// for the network and the Windows drive for the disk. No CPU temperature without admin rights: it stays 0.
pragma Singleton
import QtQuick
import QtQml
import Quickshell
import Quickshell.Io
import Quickshell.Services.UPower
import Quickshell.Windows as Win
import "../../"

Item {
    id: root

    property int cpu: 0
    property int ramPercent: 0
    property real ramGb: 0.0
    property int temp: 0
    property real netRx: 0
    property real netTx: 0
    property int diskPercent: 0
    property real diskGb: 0.0
    property real diskTotalGb: 0.0

    readonly property int historySize: 64
    property var historyUsers: ({})

    property int subscribers: 0
    readonly property bool onBattery: UPower.displayDevice.ready && UPower.displayDevice.state === UPowerDeviceState.Discharging

    function subscribe() {
        subscribers++;
        if (subscribers === 1) {
            root.netPrimed = false;
            root.sample();
        }
    }

    function unsubscribe() {
        subscribers = Math.max(0, subscribers - 1);
        if (subscribers === 0)
            root.lastNet = null;
    }

    property bool netPrimed: false

    function pushHistory(list, value) {
        let next = list.length >= root.historySize ? list.slice(1) : list.slice();
        next.push(isNaN(value) ? 0 : Number(value));
        return next;
    }

    function historyFor(interval) {
        for (let i = 0; i < historyTracks.count; i++) {
            let track = historyTracks.objectAt(i);
            if (track && track.interval === interval) return track;
        }
        return null;
    }

    function acquireHistory(interval) {
        let users = root.historyUsers[interval] || 0;
        if (users === 0) {
            let exists = false;
            for (let i = 0; i < historyModel.count; i++) {
                if (historyModel.get(i).interval === interval) exists = true;
            }
            if (!exists) historyModel.append({ interval: interval });
        }
        root.historyUsers[interval] = users + 1;
    }

    function releaseHistory(interval) {
        root.historyUsers[interval] = Math.max(0, (root.historyUsers[interval] || 0) - 1);
        if (root.historyUsers[interval] === 0) historyPruneTimer.restart();
    }

    ListModel {
        id: historyModel
    }

    Timer {
        id: historyPruneTimer
        interval: 3000
        onTriggered: {
            for (let i = historyModel.count - 1; i >= 0; i--) {
                let interval = historyModel.get(i).interval;
                if (!root.historyUsers[interval]) {
                    delete root.historyUsers[interval];
                    historyModel.remove(i);
                }
            }
        }
    }

    Instantiator {
        id: historyTracks
        model: historyModel

        // each history keeps its own beat and takes the latest sample on it
        delegate: QtObject {
            id: track
            required property int interval
            property var cpu: []
            property var ramPercent: []
            property var ramGb: []
            property var temp: []

            property Timer tick: Timer {
                interval: Math.max(1, track.interval) * 1000
                repeat: true
                running: true
                triggeredOnStart: true
                onTriggered: {
                    if (root.subscribers === 0)
                        root.sample();
                    track.cpu = root.pushHistory(track.cpu, root.cpu);
                    track.ramPercent = root.pushHistory(track.ramPercent, root.ramPercent);
                    track.ramGb = root.pushHistory(track.ramGb, root.ramGb);
                    track.temp = root.pushHistory(track.temp, root.temp);
                }
            }
        }
    }

    property var lastCpu: null
    property var lastNet: null

    function sample() {
        const s = Win.FsUtils.systemStats();
        if (s && s.total) {
            if (root.lastCpu) {
                const busy = (s.kernel - root.lastCpu.kernel) + (s.user - root.lastCpu.user);
                const idle = s.idle - root.lastCpu.idle;
                if (busy > 0)
                    root.cpu = Math.max(0, Math.min(100, Math.round((1 - idle / busy) * 100)));
            }
            root.lastCpu = { idle: s.idle, kernel: s.kernel, user: s.user };
            root.ramPercent = Math.round((1 - s.available / s.total) * 100);
            root.ramGb = Math.round((s.total - s.available) / 1073741824 * 10) / 10;
        }
        if (!netProc.running)
            netProc.running = true;
    }

    function prewarm() {
        root.sample();
        if (!diskProc.running)
            diskProc.running = true;
    }

    property int streamInterval: root.onBattery ? 4 : 2

    Timer {
        interval: root.streamInterval * 1000
        repeat: true
        running: root.subscribers > 0
        onTriggered: root.sample()
    }
    Timer {
        interval: 60000
        repeat: true
        triggeredOnStart: true
        running: root.subscribers > 0
        onTriggered: if (!diskProc.running) diskProc.running = true
    }

    // totals since boot: the first line with two numbers (the "Bytes" line; its name is translated on other languages)
    Process {
        id: netProc
        command: ["netstat", "-e"]
        stdout: StdioCollector {
            onStreamFinished: {
                const m = this.text.match(/^\s*\S[^\d\n]*\s(\d{4,})\s+(\d{4,})\s*$/m);
                if (!m)
                    return;
                const now = Date.now(), rx = Number(m[1]), tx = Number(m[2]);
                if (root.lastNet && now > root.lastNet.t) {
                    const sec = (now - root.lastNet.t) / 1000;
                    root.netRx = Math.max(0, (rx - root.lastNet.rx) / sec);
                    root.netTx = Math.max(0, (tx - root.lastNet.tx) / sec);
                    root.netPrimed = true;
                }
                root.lastNet = { t: now, rx: rx, tx: tx };
            }
        }
    }

    Process {
        id: diskProc
        command: ["powershell", "-NoProfile", "-NonInteractive", "-Command", "$d = Get-PSDrive ($env:SystemDrive.TrimEnd(\":\")); \"$($d.Used) $($d.Free)\""]
        stdout: StdioCollector {
            onStreamFinished: {
                const p = this.text.trim().split(/\s+/).map(Number);
                if (p.length < 2 || isNaN(p[0]) || isNaN(p[1]) || p[0] + p[1] <= 0)
                    return;
                root.diskGb = Math.round(p[0] / 1073741824 * 10) / 10;
                root.diskTotalGb = Math.round((p[0] + p[1]) / 1073741824 * 10) / 10;
                root.diskPercent = Math.round(p[0] / (p[0] + p[1]) * 100);
            }
        }
    }
}
