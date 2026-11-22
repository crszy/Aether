// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// The shape library of DarkShadow's Shell, drawn our own way (Settings > Appearance > Shapes shows them all).
// Round:    circle, oval, pill, semicircle, arch, fan, bun, heart, ghostish
// Polygons: square, slanted, arrow, triangle, diamond, pentagon, hexagon, gem (all with round corners)
// Wavy:     cookie (= cookie9), cookie4, cookie6, cookie7, cookie12, sun, verysunny, flower, clover (= clover4),
//           clover8, puffy, puffydiamond, clamshell
// Spiky:    burst, softburst, boom, softboom
// Pixel:    pixelcircle, pixeltriangle
// Filled with `color`, optional outline. Drawn once; it only repaints when its size, kind or color changes
// (turning it is done by rotating the item, which costs nothing).
Canvas {
    id: b
    property string kind: "cookie"
    property color color: "#33363a"
    property color line: "transparent"
    property real lineWidth: 0
    property real corner: 0.22          // polygons: how round the corners are (0 .. 0.5 of the side)
    readonly property var kinds: ["circle", "square", "slanted", "arch", "fan", "arrow", "semicircle", "oval", "pill",
        "triangle", "diamond", "clamshell", "pentagon", "hexagon", "gem", "sun", "verysunny", "cookie4", "cookie6",
        "cookie7", "cookie", "cookie12", "ghostish", "clover", "clover8", "burst", "softburst", "boom", "softboom",
        "flower", "puffy", "puffydiamond", "pixelcircle", "pixeltriangle", "bun", "heart"]
    renderStrategy: Canvas.Cooperative
    onKindChanged: requestPaint()
    onColorChanged: requestPaint()
    onLineChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const c = getContext("2d")
        c.reset()
        trace(c, width, height, Math.max(1, lineWidth))
        c.fillStyle = color
        c.fill()
        if (lineWidth > 0) { c.strokeStyle = line; c.lineWidth = lineWidth; c.stroke() }
    }

    // ---------- the families ----------
    // radius at angle a of a wavy or spiky shape (1 = touches the edge); null when the kind is not one of them
    function radial(k, a) {
        const P = Math.PI
        function spike(n, x) { return Math.abs(Math.asin(Math.sin(n * x / 2))) * 2 / P }   // 0 at a tip, 1 between
        switch (k) {
        case "cookie": return 0.92 + 0.08 * Math.cos(9 * a)
        case "cookie4": return 0.86 + 0.14 * Math.cos(4 * a)
        case "cookie6": return 0.89 + 0.11 * Math.cos(6 * a)
        case "cookie7": return 0.9 + 0.1 * Math.cos(7 * a - P / 2)
        case "cookie12": return 0.94 + 0.06 * Math.cos(12 * a)
        case "sun": return 0.95 + 0.05 * Math.cos(16 * a)
        case "verysunny": return 0.8 + 0.2 * Math.pow((1 + Math.cos(8 * a)) / 2, 1.6)
        case "flower": return 0.8 + 0.2 * Math.pow(Math.abs(Math.cos(4 * a)), 0.7)
        case "clover": return 0.7 + 0.3 * Math.pow(Math.abs(Math.cos(2 * a + P / 4)), 0.55)
        case "clover8": return 0.76 + 0.24 * Math.pow(Math.abs(Math.cos(4 * a)), 0.5)
        case "puffy": return 0.86 + 0.14 * Math.pow(Math.abs(Math.cos(3 * a)), 0.45)
        case "puffydiamond": return (0.84 + 0.16 * Math.pow(Math.abs(Math.cos(6 * a)), 0.5)) * (0.78 + 0.22 * Math.pow(Math.abs(Math.cos(2 * a)), 2))
        case "burst": return 1 - 0.3 * spike(12, a)
        case "softburst": return 1 - 0.24 * Math.pow(spike(10, a), 0.6)
        case "boom": return 1 - 0.36 * spike(16, a)
        case "softboom": return 1 - 0.22 * Math.pow(spike(14, a), 0.55)
        case "circle": return 1
        default: return null
        }
    }
    // polygons with round corners: points in -1 .. 1
    function poly(k) {
        switch (k) {
        case "square": return [[-0.86, -0.86], [0.86, -0.86], [0.86, 0.86], [-0.86, 0.86]]
        case "slanted": return [[-0.6, -0.86], [0.96, -0.86], [0.6, 0.86], [-0.96, 0.86]]
        case "arrow": return [[0, -1], [0.95, 0.9], [0, 0.45], [-0.95, 0.9]]
        case "triangle": return [[0, -0.95], [0.98, 0.75], [-0.98, 0.75]]
        case "diamond": return [[0, -1], [0.8, 0], [0, 1], [-0.8, 0]]
        case "gem": return [[-0.5, -0.9], [0.5, -0.9], [1, 0], [0.5, 0.9], [-0.5, 0.9], [-1, 0]]
        case "pentagon": { const o = []; for (let i = 0; i < 5; i++) { const a = -Math.PI / 2 + i * 2 * Math.PI / 5; o.push([Math.cos(a), Math.sin(a) + 0.1]) } return o }
        case "hexagon": { const o = []; for (let i = 0; i < 6; i++) { const a = -Math.PI / 2 + i * Math.PI / 3; o.push([Math.cos(a) * 0.98, Math.sin(a)]) } return o }
        default: return null
        }
    }
    function roundPoly(c, pts, rr) {
        const n = pts.length
        c.moveTo((pts[0][0] + pts[n - 1][0]) / 2, (pts[0][1] + pts[n - 1][1]) / 2)
        for (let i = 0; i < n; i++) {
            const p = pts[i], q = pts[(i + 1) % n]
            c.arcTo(p[0], p[1], (p[0] + q[0]) / 2, (p[1] + q[1]) / 2, rr)
        }
    }

    // the outline of the shape as a path on a 2D context (ShapeImage clips pictures with it)
    function trace(c, w, h, inset) {
        const cx = w / 2, cy = h / 2, R = Math.min(w, h) / 2 - inset
        const W = w - 2 * inset, H = h - 2 * inset, x0 = inset, y0 = inset
        c.beginPath()
        const k = kind
        if (radial(k, 0) !== null) {
            const steps = 240
            for (let i = 0; i <= steps; i++) {
                const a = i * 2 * Math.PI / steps
                const r = R * radial(k, a)
                const x = cx + r * Math.cos(a), y = cy + r * Math.sin(a)
                if (i === 0) c.moveTo(x, y); else c.lineTo(x, y)
            }
            c.closePath()
            return
        }
        const pp = poly(k)
        if (pp !== null) {
            const pts = pp.map(function(p) { return [cx + p[0] * R, cy + p[1] * R] })
            let side = 1e9
            for (let i = 0; i < pts.length; i++) { const q = pts[(i + 1) % pts.length]; side = Math.min(side, Math.hypot(q[0] - pts[i][0], q[1] - pts[i][1])) }
            roundPoly(c, pts, side * corner)
            c.closePath()
            return
        }
        switch (k) {
        case "oval": c.ellipse(x0, y0 + H * 0.15, W, H * 0.7); break
        case "pill": { const ph = Math.min(H, W * 0.55), py = cy - ph / 2, r = ph / 2
            c.moveTo(x0 + r, py); c.lineTo(x0 + W - r, py); c.arc(x0 + W - r, cy, r, -Math.PI / 2, Math.PI / 2); c.lineTo(x0 + r, py + ph); c.arc(x0 + r, cy, r, Math.PI / 2, Math.PI * 1.5); break }
        case "semicircle": { const r = Math.min(W / 2, H * 0.62) * 0.98, by = cy + r / 2
            c.moveTo(cx - r, by); c.arc(cx, by, r, Math.PI, 0); c.lineTo(cx - r, by); break }
        case "arch": { const r = W * 0.42, top = y0 + H * 0.06, bot = y0 + H * 0.94, rr = r * 0.25
            c.moveTo(cx - r, bot - rr); c.lineTo(cx - r, top + r); c.arc(cx, top + r, r, Math.PI, 0); c.lineTo(cx + r, bot - rr)
            c.arcTo(cx + r, bot, cx + r - rr, bot, rr); c.lineTo(cx - r + rr, bot); c.arcTo(cx - r, bot, cx - r, bot - rr, rr); break }
        case "fan": { const r = Math.min(W, H) * 0.92, ox = x0 + (W - r) / 2, oy = y0 + (H - r) / 2 + r, rr = r * 0.12
            c.moveTo(ox, oy - r); c.arc(ox, oy, r, -Math.PI / 2, 0); c.lineTo(ox + rr, oy); c.arcTo(ox, oy, ox, oy - rr, rr); break }
        case "bun": { const t = R * 0.62, b2 = R * 0.28, L = cx - R * 0.9, Rr = cx + R * 0.9, T = cy - R * 0.78, B = cy + R * 0.78
            c.moveTo(L, B - b2); c.lineTo(L, T + t); c.arcTo(L, T, L + t, T, t); c.lineTo(Rr - t, T); c.arcTo(Rr, T, Rr, T + t, t)
            c.lineTo(Rr, B - b2); c.arcTo(Rr, B, Rr - b2, B, b2); c.lineTo(L + b2, B); c.arcTo(L, B, L, B - b2, b2); break }
        case "heart": {
            for (let i = 0; i <= 200; i++) {
                const t = i / 200 * Math.PI * 2
                const x = 16 * Math.pow(Math.sin(t), 3), y = -(13 * Math.cos(t) - 5 * Math.cos(2 * t) - 2 * Math.cos(3 * t) - Math.cos(4 * t))
                const px = cx + x / 17 * R, py = cy + (y + 2) / 17 * R
                if (i === 0) c.moveTo(px, py); else c.lineTo(px, py)
            }
            break }
        case "ghostish": { const r = W * 0.42, top = y0 + H * 0.05, bot = y0 + H * 0.92
            c.moveTo(cx - r, bot); c.lineTo(cx - r, top + r); c.arc(cx, top + r, r, Math.PI, 0); c.lineTo(cx + r, bot)
            for (let i = 0; i <= 60; i++) { const x = cx + r - 2 * r * i / 60; c.lineTo(x, bot - Math.abs(Math.sin(i / 60 * Math.PI * 3)) * H * 0.07) }
            break }
        case "clamshell": {
            const r = R * 0.98
            for (let i = 0; i <= 120; i++) {
                const a = Math.PI + i / 120 * Math.PI
                const rr = r * (0.94 + 0.06 * Math.cos(10 * (a - Math.PI)))
                const x = cx + rr * Math.cos(a), y = cy + r * 0.35 + rr * Math.sin(a)
                if (i === 0) c.moveTo(x, y); else c.lineTo(x, y)
            }
            c.arcTo(cx + r, cy + r * 0.9, cx, cy + r * 0.9, r * 0.5); c.arcTo(cx - r, cy + r * 0.9, cx - r, cy + r * 0.35, r * 0.5)
            break }
        case "pixelcircle": case "pixeltriangle": {
            const n = 9, cell = Math.min(W, H) / n, ox = cx - cell * n / 2, oy = cy - cell * n / 2
            for (let j = 0; j < n; j++) for (let i = 0; i < n; i++) {
                const u = (i + 0.5) / n * 2 - 1, v = (j + 0.5) / n * 2 - 1
                const inside = k === "pixelcircle" ? (u * u + v * v <= 0.9) : (v > -0.85 && Math.abs(u) <= (v + 0.95) / 1.9 && v < 0.95)
                if (inside) c.rect(ox + i * cell, oy + j * cell, cell + 0.5, cell + 0.5)
            }
            return }
        default: c.arc(cx, cy, R, 0, Math.PI * 2)
        }
        c.closePath()
    }
}
