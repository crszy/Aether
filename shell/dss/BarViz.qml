// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Services.Mpris
import QtQuick

// The visualizer on the bar (Bar layout > Visualizer): a few soft bars that move with the music your speakers
// play (rice-cava). It only listens while music plays; quiet, it rests as small dots.
// vertical: for a bar on the left or right edge (the bars grow sideways from the middle).
Item {
    id: bv
    property bool active: true
    property bool vertical: false
    property color color: "#F6F5F4"
    property int count: 12
    property real thick: 22              // the room across the bar the bars may use
    property var levels: []
    readonly property bool listening: active && !Look.flat && Mpris.players.values.some(function(p) { return p.isPlaying })
    implicitWidth: vertical ? thick : count * 4 + (count - 1) * 2
    implicitHeight: vertical ? count * 4 + (count - 1) * 2 : thick

    property bool quiet: false
    onListeningChanged: if (!listening) { levels = []; quiet = false }
    CavaUser { active: bv.listening }
    Connections {
        target: CavaFeed
        enabled: bv.listening
        function onFrame() {
            const p = CavaFeed.bands
            if (p.length < 8) return
            // the 64 bands folded into a few: the low ones count most, the very high ones are left out
            const n = bv.count, use = Math.floor(p.length * 0.75), out = []
            let loud = 0
            for (let i = 0; i < n; i++) {
                const a = Math.floor(i * use / n), b = Math.max(a + 1, Math.floor((i + 1) * use / n))
                let s = 0
                for (let j = a; j < b; j++) s += p[j]
                const v = Math.min(1, s / (b - a) / 100 * 1.25)
                out.push(v)
                if (v > loud) loud = v
            }
            // a pause in the song: nothing moves, so nothing is updated
            if (loud < 0.005 && bv.quiet) return
            bv.quiet = loud < 0.005
            bv.levels = out
        }
    }

    Repeater {
        model: bv.count
        delegate: Rectangle {
            readonly property real v: bv.levels[index] !== undefined ? bv.levels[index] : 0
            readonly property real len: Math.max(4, bv.thick * (0.12 + 0.88 * v))
            // centred across the bar, so they grow both ways from the middle
            x: bv.vertical ? (bv.thick - len) / 2 : index * 6
            y: bv.vertical ? index * 6 : (bv.thick - len) / 2
            width: bv.vertical ? len : 4
            height: bv.vertical ? 4 : len
            radius: 2
            color: bv.color
            opacity: 0.45 + 0.55 * v
            Behavior on width { enabled: bv.vertical; NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
            Behavior on height { enabled: !bv.vertical; NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
            Behavior on x { enabled: bv.vertical; NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
            Behavior on y { enabled: !bv.vertical; NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
        }
    }
}
