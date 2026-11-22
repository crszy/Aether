// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

// A progress line in the Material 3 Expressive way: the done part is a moving wave, the rest a flat track.
// The wave settles near 0 % and 100 % so it never wiggles past the ends. Settings > Appearance > Components.
Item {
    id: wb
    property real value: 0.5            // 0 .. 1
    property bool wavy: Prefs.wavy
    property int thick: Prefs.progressThick
    property color color: M3.primary
    property color track: M3.surfaceHighest
    property bool running: visible
    property real amp: wavy ? Math.min(height / 2 - thick, 4 + thick * 0.4) * Math.sin(Math.PI * Math.max(0, Math.min(1, value))) * 1.15 : 0
    property real phase: 0
    implicitWidth: 200
    implicitHeight: 18
    NumberAnimation on phase { from: 0; to: Math.PI * 2; duration: 1400; loops: Animation.Infinite; running: wb.running && wb.wavy }
    Behavior on value { NumberAnimation { duration: M3.d(420); easing.type: Easing.OutCubic } }
    onPhaseChanged: cv.requestPaint()
    onValueChanged: cv.requestPaint()
    onThickChanged: cv.requestPaint()
    onWavyChanged: cv.requestPaint()
    onColorChanged: cv.requestPaint()
    onWidthChanged: cv.requestPaint()
    Canvas {
        id: cv
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const c = getContext("2d")
            c.reset()
            const w = width, h = height, mid = h / 2, t = wb.thick
            const end = Math.max(t, (w - t) * Math.max(0, Math.min(1, wb.value)))
            c.lineCap = "round"
            c.lineWidth = t
            // the rest: a flat track after a small gap, with a stop dot at the end
            if (end + t * 2 + 4 < w - t / 2) {
                c.strokeStyle = wb.track
                c.beginPath(); c.moveTo(end + t * 2 + 4, mid); c.lineTo(w - t / 2, mid); c.stroke()
                c.fillStyle = wb.color
                c.beginPath(); c.arc(w - t / 2, mid, Math.max(1.5, t / 2 - 0.5), 0, Math.PI * 2); c.fill()
            }
            // the done part
            c.strokeStyle = wb.color
            c.beginPath()
            const wl = 22 + t * 2
            for (let x = t / 2; x <= end; x += 2) {
                const y = mid + wb.amp * Math.sin((x / wl) * Math.PI * 2 - wb.phase)
                if (x === t / 2) c.moveTo(x, y); else c.lineTo(x, y)
            }
            c.stroke()
        }
    }
}
