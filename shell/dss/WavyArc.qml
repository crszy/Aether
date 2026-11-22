// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// A ring of progress around something round (album art): the part that is done is a wave that moves while it
// plays, the rest a calm track. Flat when "Wavy progress" is off.
Item {
    id: wa
    property real value: 0
    property bool moving: true
    property bool wavy: Look.wavy
    property real thick: Look.waveThick
    property color color: "#9ec9bd"
    property color track: Qt.rgba(1, 1, 1, 0.12)
    property real waves: 14
    property real phase: 0
    // the wave moves about 30 times a second, not once every frame of the screen (on a 144 or 240 Hz screen that
    // was a picture drawn by the processor up to 240 times a second for every moving wave: V4.84)
    Timer {
        interval: 33; repeat: true
        running: wa.visible && wa.moving && wa.wavy && !Look.lite
        onTriggered: wa.phase = (Date.now() % 2200) / 2200 * Math.PI * 2
    }
    onPhaseChanged: cv.requestPaint()
    onValueChanged: cv.requestPaint()
    onThickChanged: cv.requestPaint()
    onWavyChanged: cv.requestPaint()
    onColorChanged: cv.requestPaint()
    Canvas {
        id: cv
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const c = getContext("2d"); c.reset()
            const t = wa.thick, amp = wa.wavy ? 1.6 + t * 0.35 : 0
            const cx = width / 2, cy = height / 2, r = Math.min(width, height) / 2 - t / 2 - amp - 1
            const f = Math.max(0.004, Math.min(1, wa.value)), s = -Math.PI / 2, len = Math.PI * 2 * f
            c.lineCap = "round"; c.lineWidth = t
            if (f < 0.99) {
                c.strokeStyle = wa.track
                c.beginPath(); c.arc(cx, cy, r, s + len + 0.22, s + Math.PI * 2 - 0.22); c.stroke()
            }
            c.strokeStyle = wa.color
            c.beginPath()
            const n = Math.max(12, Math.round(len * r / 2))
            for (let i = 0; i <= n; i++) {
                const a = s + len * i / n
                const k = Math.min(1, i / n * 8, (1 - i / n) * 8)     // calm at both ends
                const rr = r + amp * k * Math.sin(a * wa.waves - wa.phase)
                const x = cx + rr * Math.cos(a), y = cy + rr * Math.sin(a)
                if (i === 0) c.moveTo(x, y); else c.lineTo(x, y)
            }
            c.stroke()
        }
    }
}
