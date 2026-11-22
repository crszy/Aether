// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// The Edge style of the bar: a part of the bar that is one piece with the screen edge. Where it meets the edge it
// bends in softly (ears), its far corners are round, and a side that reaches the screen's own side stays straight.
// openL / openR: that side reaches the screen's side. flip: for a bar at the bottom.
Item {
    id: h
    property color fill: "#121316"
    property bool flip: false
    property bool openL: false
    property bool openR: false
    property bool shadow: true
    property real ear: 16
    property real corner: 18
    function path(c, W, H, e, r) {
        const a = h.openL ? 0 : e, b = h.openR ? W : W - e
        c.beginPath()
        if (h.openL) { c.moveTo(0, 0); c.lineTo(0, H) }
        else { c.moveTo(0, 0); c.arcTo(a, 0, a, e, e); c.lineTo(a, H - r); c.arcTo(a, H, a + r, H, r) }
        if (h.openR) { c.lineTo(W, H); c.lineTo(W, 0) }
        else { c.lineTo(b - r, H); c.arcTo(b, H, b, H - r, r); c.lineTo(b, e); c.arcTo(b, 0, W, 0, e) }
        c.closePath()
    }
    Canvas {
        id: cv
        anchors.fill: parent
        anchors.bottomMargin: -8
        transform: Scale { origin.y: h.height / 2; yScale: h.flip ? -1 : 1 }
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const c = getContext("2d"); c.reset()
            const W = width, H = h.height, e = h.ear, r = Math.min(h.corner, (H - e) / 1.1)
            if (h.shadow) {
                c.save(); c.translate(0, 2); c.lineJoin = "round"
                const st = [[10, 0.05], [7, 0.06], [4, 0.07], [2, 0.08]]
                for (let i = 0; i < st.length; i++) { h.path(c, W, H, e, r); c.lineWidth = st[i][0]; c.strokeStyle = "rgba(0,0,0," + st[i][1] + ")"; c.stroke() }
                c.restore()
                c.save(); c.globalCompositeOperation = "destination-out"; h.path(c, W, H, e, r); c.fillStyle = "#000"; c.fill(); c.restore()
            }
            h.path(c, W, H, e, r)
            c.fillStyle = h.fill
            c.fill()
        }
    }
    onFillChanged: cv.requestPaint()
    onWidthChanged: cv.requestPaint()
    onHeightChanged: cv.requestPaint()
    onShadowChanged: cv.requestPaint()
    onOpenLChanged: cv.requestPaint()
    onOpenRChanged: cv.requestPaint()
    onVisibleChanged: if (visible) cv.requestPaint()
}
