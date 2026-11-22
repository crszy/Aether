// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import QtQuick

// Audio visualizer (cava). No background; styles: bars (straight bars like cava in a terminal), mirror, wave, dots.
// colors: accent (your theme color), gradient (it shifts along the bars), rainbow, mono (white, or black on a light wallpaper).
// flip: grow downwards instead of upwards.
Item {
    id: vz
    property bool running: true
    property string style: "bars"
    property string colorMode: "accent"
    property bool flip: false
    property var pal: null            // the widget's palette: tells if the wallpaper behind is light
    property var bars: []
    // Edit mode > Visualizer: how many bars, their ends (square, rounded, pill) and the space between them
    property int nBars: 64
    property string shape: "square"
    property string spacing: "normal"
    property real gain: 1              // how tall the bars go (Edit mode > Visualizer > Height of the bars)
    onGainChanged: cv.requestPaint()
    readonly property int count: nBars === 16 || nBars === 32 || nBars === 48 ? nBars : 64
    readonly property real pitch: width / count
    readonly property real gapK: spacing === "thin" ? 0.16 : (spacing === "wide" ? 0.5 : 0.32)
    readonly property real gap: style === "dots" ? 4 : (style === "wave" ? 3 : Math.max(2, Math.min(28, pitch * gapK)))
    // the level of bar i: the loudest of the cava bars it covers
    function level(b, i) {
        const k = 64 / count
        let m = 0
        for (let j = Math.floor(i * k); j < Math.floor((i + 1) * k); j++) if ((b[j] || 0) > m) m = b[j]
        return Math.min(100, m * gain)
    }
    onNBarsChanged: vz.refreshLater()
    onShapeChanged: cv.requestPaint()
    onSpacingChanged: cv.requestPaint()
    readonly property real bw: Math.max(1, (width - gap * (count - 1)) / count)

    readonly property bool lightBg: pal ? pal.light : false
    readonly property color acc: Look.accent
    readonly property real hue: acc.hslHue < 0 ? 0 : acc.hslHue
    readonly property real sat: acc.hslSaturation < 0.08 ? acc.hslSaturation : Math.max(0.5, Math.min(0.95, acc.hslSaturation))
    readonly property real tone: lightBg ? 0.38 : 0.72
    // the color of bar i (0..count-1)
    function colorAt(i) {
        const t = count > 1 ? i / (count - 1) : 0
        switch (colorMode) {
        case "gradient": return Qt.hsla((hue + 0.22 * t) % 1, Math.max(sat, 0.55), tone, 1)
        case "rainbow": return Qt.hsla(t * 0.85, 0.75, lightBg ? 0.42 : 0.68, 1)
        case "mono": return lightBg ? Qt.rgba(0.08, 0.08, 0.1, 1) : Qt.rgba(1, 1, 1, 1)
        default: return Qt.hsla(hue, sat, tone, 1)
        }
    }
    // the colour of every bar, worked out once (when the theme, the colours or the number of bars change), not
    // 30 times a second for every bar
    property var cols: []
    function refresh() {
        const o = []
        for (let i = 0; i < count; i++) { const k = colorAt(i); o.push("rgb(" + Math.round(k.r * 255) + "," + Math.round(k.g * 255) + "," + Math.round(k.b * 255) + ")") }
        cols = o
        cv.requestPaint()
    }
    Component.onCompleted: refresh()
    function css(c, a) { return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + a + ")" }
    // the colours are worked out after the change has gone through everything (Qt.callLater): read straight from
    // the accent's change signal, hue and sat were still the old ones, so the bars kept the previous wallpaper's
    // colour until the next change
    function refreshLater() { Qt.callLater(vz.refresh) }
    onColorModeChanged: vz.refreshLater()
    onStyleChanged: cv.requestPaint()
    onFlipChanged: cv.requestPaint()
    onWidthChanged: cv.requestPaint()
    onHeightChanged: cv.requestPaint()
    onLightBgChanged: vz.refreshLater()
    onAccChanged: vz.refreshLater()
    onHueChanged: vz.refreshLater()
    onSatChanged: vz.refreshLater()
    onToneChanged: vz.refreshLater()

    readonly property bool listening: running && visible
    CavaUser { active: vz.listening }
    Connections {
        target: CavaFeed
        enabled: vz.listening
        function onFrame() {
            const p = CavaFeed.bands
            if (p.length === 0) return
            // rises at once, falls softly
            const o = vz.bars, q = []
            let loud = 0
            for (let i = 0; i < p.length; i++) { const a = o[i] !== undefined ? o[i] : 0; const v = p[i] >= a ? p[i] : a * 0.8 + p[i] * 0.2; q.push(v); if (v > loud) loud = v }
            // silence: draw the empty picture once, then nothing (no work 30 times a second for no sound)
            if (loud < 0.5 && vz.quiet) return
            vz.quiet = loud < 0.5
            vz.bars = q
            cv.requestPaint()
        }
    }

    // everything is drawn here
    property bool quiet: false
    Canvas {
        id: cv
        anchors.fill: parent
        onVisibleChanged: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            const n = vz.count, W = width, H = height, b = vz.bars
            if (!b || b.length === 0) return
            if (vz.style === "bars" || vz.style === "mirror") {
                // straight bars with flat tops, like cava in a terminal: one soft fade over the whole height,
                // a little dimmer at the base, brightest in the middle, fading toward the tip
                const mir = vz.style === "mirror"
                const span = mir ? H / 2 : H
                const rr = vz.shape === "pill" ? 0.5 : (vz.shape === "rounded" ? 0.3 : 0)
                const w = Math.max(1, Math.round(vz.bw))
                const r = Math.min(w / 2, Math.max(0, w * rr), 14)
                const cols = vz.cols
                const one = vz.colorMode === "accent" || vz.colorMode === "mono"
                function bar(bx, by, bw2, bh2) { if (r > 0.5) c.roundedRect(bx, by, bw2, bh2, r, r); else c.rect(bx, by, bw2, bh2) }
                if (one) { c.fillStyle = cols[0] || "#fff"; c.beginPath() }
                for (let i = 0; i < n; i++) {
                    const v = Math.max(0, Math.min(1, vz.level(b, i) / 100))
                    const h = Math.max(Math.max(2, r * 2), Math.round(v * span))
                    const x = Math.round(i * (vz.bw + vz.gap))
                    if (!one) { c.fillStyle = cols[i] || "#fff"; c.beginPath() }
                    if (mir) { bar(x, H / 2 - h, w, h); bar(x, H / 2, w, h) }
                    else bar(x, vz.flip ? 0 : H - h, w, h)
                    if (!one) c.fill()
                }
                if (one) c.fill()
                // one soft fade over the whole height: a little dimmer at the base, brightest in the middle,
                // fading toward the tip (two gradients a frame instead of one or two for every bar)
                c.globalCompositeOperation = "destination-in"
                // over the whole picture at once; mirror: the same fade both ways from the middle
                const g = c.createLinearGradient(0, vz.flip && !mir ? H : 0, 0, vz.flip && !mir ? 0 : H)
                if (mir) {
                    g.addColorStop(0, "rgba(0,0,0,0.3)"); g.addColorStop(0.25, "rgba(0,0,0,1)"); g.addColorStop(0.5, "rgba(0,0,0,0.82)")
                    g.addColorStop(0.75, "rgba(0,0,0,1)"); g.addColorStop(1, "rgba(0,0,0,0.3)")
                } else {
                    g.addColorStop(0, "rgba(0,0,0,0.3)"); g.addColorStop(0.5, "rgba(0,0,0,1)"); g.addColorStop(1, "rgba(0,0,0,0.82)")
                }
                c.fillStyle = g
                c.fillRect(0, 0, W, H)
                c.globalCompositeOperation = "source-over"
            } else if (vz.style === "wave") {
                const pts = []
                for (let i = 0; i < n; i++) {
                    const v = vz.level(b, i) / 100
                    const x = i * W / (n - 1)
                    pts.push([x, vz.flip ? v * H * 0.95 : H - v * H * 0.95])
                }
                const base = vz.flip ? 0 : H
                // the fill fades toward the base; the colors run along the wave
                const fill = c.createLinearGradient(0, 0, W, 0)
                const line = c.createLinearGradient(0, 0, W, 0)
                for (let s = 0; s <= 8; s++) {
                    const col = vz.colorAt(Math.round(s / 8 * (n - 1)))
                    fill.addColorStop(s / 8, vz.css(col, 0.30))
                    line.addColorStop(s / 8, vz.css(col, 0.95))
                }
                c.beginPath()
                c.moveTo(0, base)
                c.lineTo(pts[0][0], pts[0][1])
                for (let i = 1; i < n; i++) {
                    const mx = (pts[i - 1][0] + pts[i][0]) / 2, my = (pts[i - 1][1] + pts[i][1]) / 2
                    c.quadraticCurveTo(pts[i - 1][0], pts[i - 1][1], mx, my)
                }
                c.lineTo(W, pts[n - 1][1])
                c.lineTo(W, base)
                c.closePath()
                c.fillStyle = fill
                c.fill()
                c.beginPath()
                c.moveTo(pts[0][0], pts[0][1])
                for (let i = 1; i < n; i++) {
                    const mx = (pts[i - 1][0] + pts[i][0]) / 2, my = (pts[i - 1][1] + pts[i][1]) / 2
                    c.quadraticCurveTo(pts[i - 1][0], pts[i - 1][1], mx, my)
                }
                c.lineWidth = 3
                c.lineCap = "round"
                c.strokeStyle = line
                c.stroke()
            } else {
                const d = Math.max(2, Math.min(vz.bw, 10))
                const step = d + 3
                for (let i = 0; i < n; i++) {
                    const v = vz.level(b, i) / 100
                    const k = Math.max(1, Math.round(v * H / step))
                    const x = i * (vz.bw + vz.gap) + vz.bw / 2
                    c.fillStyle = vz.cols[i] || "#fff"
                    for (let j = 0; j < k; j++) {
                        const y = vz.flip ? d / 2 + j * step : H - d / 2 - j * step
                        c.globalAlpha = 0.35 + 0.6 * (1 - j / Math.max(1, k))
                        c.beginPath()
                        c.arc(x, y, d / 2, 0, Math.PI * 2)
                        c.fill()
                    }
                }
                c.globalAlpha = 1
            }
        }
    }
}
