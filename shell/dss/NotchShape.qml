// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// The notch: a shape that grows out of the screen edge, with soft inward curves (ears) where it meets the edge
// and round corners at the far side. flip: for a bar at the bottom. shadow: a soft shadow along its far side.
Item {
    id: n
    property color fill: "#050506"
    property bool flip: false
    property bool shadow: true
    property real ear: 14
    property real corner: 20
    function path(c, W, H, e, r, o) {
        c.beginPath()
        c.moveTo(0, 0)
        c.arcTo(e, 0, e, e, e)
        c.lineTo(e, H - r - o)
        c.arcTo(e, H - o, e + r, H - o, r)
        c.lineTo(W - e - r, H - o)
        c.arcTo(W - e, H - o, W - e, H - r - o, r)
        c.lineTo(W - e, e)
        c.arcTo(W - e, 0, W, 0, e)
        c.closePath()
    }
    Canvas {
        id: cv
        anchors.fill: parent
        anchors.bottomMargin: -8          // room for the shadow
        transform: Scale { origin.y: n.height / 2; yScale: n.flip ? -1 : 1 }
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const c = getContext("2d"); c.reset()
            const W = width, H = n.height, e = n.ear, r = Math.min(n.corner, (H - e) / 1.2)
            // the shadow only outside the shape, so a see-through notch stays evenly see-through (and blurred).
            // A few see-through outlines of growing width, not shadowBlur: that one is blurred by the CPU (70-140 ms
            // for every repaint, and the notch repaints whenever its width moves: the bar dropped to 2-3 fps)
            if (n.shadow) {
                c.save(); c.translate(0, 2); c.lineJoin = "round"
                const st = [[11, 0.05], [8, 0.06], [5, 0.07], [2.4, 0.08]]
                for (let i = 0; i < st.length; i++) {
                    n.path(c, W, H, e, r, 0); c.lineWidth = st[i][0]; c.strokeStyle = "rgba(0,0,0," + st[i][1] + ")"; c.stroke()
                }
                c.restore()
                c.save(); c.globalCompositeOperation = "destination-out"
                n.path(c, W, H, e, r, 0); c.fillStyle = "#000"; c.fill(); c.restore()
            }
            n.path(c, W, H, e, r, 0)
            c.fillStyle = n.fill
            c.fill()
        }
    }
    onFillChanged: cv.requestPaint()
    onWidthChanged: cv.requestPaint()
    onHeightChanged: cv.requestPaint()
    onShadowChanged: cv.requestPaint()
    onVisibleChanged: if (visible) cv.requestPaint()
}
