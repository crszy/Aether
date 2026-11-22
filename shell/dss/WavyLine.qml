// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// A progress line in the Material 3 Expressive way: the part that is done is a wave that moves while it plays,
// the rest a calm track with a dot at the end. The wave settles near 0 % and 100 %. Flat when you turn
// "Wavy progress" off (Settings > Appearance > Components). Click or drag it to seek (moved(v)).
Item {
    id: wl
    property real value: 0                // 0 .. 1
    property bool moving: true            // the wave travels (music playing ...)
    property bool wavy: Look.wavy
    property real thick: Look.waveThick
    property color color: "#9ec9bd"
    property color track: Qt.rgba(1, 1, 1, 0.14)
    property bool handle: true
    property bool seekable: false
    signal moved(real v)
    implicitHeight: 18
    property real phase: 0
    readonly property real shown: drag.pressed ? drag.v : value
    // the wave moves about 30 times a second, not once every frame of the screen (on a 144 or 240 Hz screen that
    // was a picture drawn by the processor up to 240 times a second for every moving wave: V4.84)
    Timer {
        interval: 33; repeat: true
        running: wl.visible && wl.moving && wl.wavy && !Look.lite
        onTriggered: wl.phase = (Date.now() % 1600) / 1600 * Math.PI * 2
    }
    onPhaseChanged: cv.requestPaint()
    onShownChanged: cv.requestPaint()
    onThickChanged: cv.requestPaint()
    onWavyChanged: cv.requestPaint()
    onColorChanged: cv.requestPaint()
    onWidthChanged: cv.requestPaint()
    Canvas {
        id: cv
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const c = getContext("2d"); c.reset()
            const w = width, mid = height / 2, t = wl.thick
            const f = Math.max(0, Math.min(1, wl.shown))
            const hx = Math.max(t / 2, (w - t) * f)
            const gap = wl.handle ? 7 : 4
            c.lineCap = "round"; c.lineWidth = t
            // the rest
            if (hx + gap + t < w - t / 2) {
                c.strokeStyle = wl.track
                c.beginPath(); c.moveTo(hx + gap + t / 2, mid); c.lineTo(w - t / 2, mid); c.stroke()
                c.fillStyle = wl.color
                c.beginPath(); c.arc(w - t / 2, mid, Math.max(1.5, t / 2 - 0.5), 0, Math.PI * 2); c.fill()
            }
            // the part that is done
            const amp = wl.wavy ? Math.min(mid - t / 2 - 1, 2.2 + t * 0.45) * Math.sin(Math.PI * f) : 0
            const wlen = 20 + t * 2.5
            c.strokeStyle = wl.color
            c.beginPath()
            for (let x = t / 2; x <= hx - (wl.handle ? gap : 0); x += 2) {
                const y = mid + amp * Math.sin(x / wlen * Math.PI * 2 - wl.phase)
                if (x === t / 2) c.moveTo(x, y); else c.lineTo(x, y)
            }
            c.stroke()
            // the handle: a short upright bar
            if (wl.handle) {
                c.lineWidth = Math.max(3, t)
                c.beginPath(); c.moveTo(hx, mid - height / 2 + 2); c.lineTo(hx, mid + height / 2 - 2); c.stroke()
            }
        }
    }
    MouseArea {
        id: drag
        anchors.fill: parent
        anchors.topMargin: -6; anchors.bottomMargin: -6
        enabled: wl.seekable
        cursorShape: wl.seekable ? Qt.PointingHandCursor : Qt.ArrowCursor
        property real v: 0
        function at(x) { return Math.max(0, Math.min(1, x / width)) }
        onPressed: function(m) { v = at(m.x) }
        onPositionChanged: function(m) { if (pressed) v = at(m.x) }
        onReleased: wl.moved(v)
    }
}
