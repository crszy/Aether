pragma Singleton
pragma ComponentBehavior: Bound

// Caelestia for Windows: DMS's CavaService on Quickshell.Windows' AudioVisualizer (WASAPI loopback, shared with
// Caelestia and Noctalia) instead of running the Linux `cava` through `sh`. Same API: values (6 ints, 0..1000 like
// cava's ascii output), refCount, cavaAvailable. The shared visualizer's bars are grouped into 6 here - its bar count
// is never changed (another shell may be using it).
import QtQuick
import Quickshell
import Quickshell.Windows as Win
import qs.services as CaeSvc
import qs.dms.Common

Singleton {
    id: root

    property list<int> values: Array(6).fill(0)
    property int refCount: 0
    readonly property bool cavaAvailable: true
    readonly property bool cavaWanted: SettingsData.audioVisualizerEnabled || refCount > 0
    // Aether: off while game mode stops the visualizer (Settings > Windows extras)
    readonly property bool paused: CaeSvc.WinSettings.gameStopVisualizer
    readonly property bool active: (refCount > 0 || lingerTimer.running) && !paused

    onRefCountChanged: {
        if (refCount > 0) {
            lingerTimer.stop();
            return;
        }
        values = Array(6).fill(0);
        lingerTimer.restart();
    }

    // switch the shared visualizer on when DMS wants it, never off (Caelestia / Noctalia may use it too)
    onActiveChanged: if (active) Win.AudioVisualizer.running = true

    Timer {
        id: lingerTimer

        interval: 3000
    }

    // 25 updates a second, like DMS's cava config
    Timer {
        interval: 40
        repeat: true
        running: root.active
        onTriggered: {
            const src = Win.AudioVisualizer.values ?? [];
            const max = Math.max(1, Win.AudioVisualizer.maxValue);
            if (!src.length)
                return;
            const out = [];
            for (let g = 0; g < 6; g++) {
                const a = Math.floor(g * src.length / 6), b = Math.max(a + 1, Math.floor((g + 1) * src.length / 6));
                let peak = 0;
                for (let i = a; i < b && i < src.length; i++)
                    peak = Math.max(peak, src[i]);
                out.push(Math.round(Math.max(0, Math.min(1, peak / max)) * 1000));
            }
            if (!out.every((v, i) => v === root.values[i]))
                root.values = out;
        }
    }
}
