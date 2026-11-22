// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// The DarkShadow's Shell logo, alive: a black hole. A glowing disk of matter turns around a perfectly black
// shadow, the light from behind it bends over the top (the halo), a thin photon ring shines at its edge, bits of
// matter spiral in, and a few stars twinkle around. No background, so it sits on anything.
// (The file keeps its old name, MoonLogo, so every place that shows the logo got the new one at once.)
//   size     width and height
//   running  play the animations (pause them when the logo is not on screen)
//   intro    draw itself in when it appears (the disk spins up, the shadow opens, then the stars pop up)
Item {
    id: logo
    property real size: 140
    property bool running: visible
    property bool intro: false
    // the stars and the falling bits around it (off for small places, like the bar)
    property bool extras: true
    // the colors of the disk: white-hot inside, amber, then a violet edge
    property color hot: "#fff6e4"
    property color warm: "#ffb35c"
    property color cool: "#a46bff"
    property color glowColor: "#ff9d4d"
    // changes whenever a color changes: the drawings are made again
    readonly property string colorKey: "" + hot + warm + cool + glowColor
    // 0 → 1 while it draws in (stays 1 without intro)
    property real reveal: intro ? 0 : 1

    width: size
    height: size

    // the disk's tilt: 1 is seen from above, small is seen edge-on
    readonly property real tilt: 0.22
    readonly property real shadowR: size * 0.16
    readonly property real diskR: size * 0.47

    function replay() { reveal = 0; revealAnim.restart() }
    NumberAnimation {
        id: revealAnim
        target: logo; property: "reveal"; from: 0; to: 1
        duration: 1700; easing.type: Easing.OutCubic
    }
    Component.onCompleted: if (intro && visible) revealAnim.start()
    onVisibleChanged: if (intro && visible && reveal === 0 && !revealAnim.running) revealAnim.start()

    // the disk turns: one turn every 9 s (it spins up during the intro)
    property real spin: 0
    NumberAnimation on spin {
        running: logo.running; loops: Animation.Infinite
        from: 0; to: 360; duration: 9000
    }
    readonly property real angle: spin + (1 - reveal) * -240

    // ---- warm glow around everything
    Canvas {
        property string ck: logo.colorKey; onCkChanged: requestPaint()
        anchors.centerIn: parent
        width: logo.size * 1.3
        height: width
        opacity: 0.75 * logo.reveal
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const g = ctx.createRadialGradient(width / 2, height / 2, 0, width / 2, height / 2, width / 2)
            const c = logo.glowColor
            g.addColorStop(0, Qt.rgba(c.r, c.g, c.b, 0.34))
            g.addColorStop(0.35, Qt.rgba(c.r, c.g, c.b, 0.12))
            g.addColorStop(1, Qt.rgba(c.r, c.g, c.b, 0))
            ctx.fillStyle = g
            ctx.fillRect(0, 0, width, height)
        }
        SequentialAnimation on scale {
            running: logo.running; loops: Animation.Infinite
            NumberAnimation { from: 0.94; to: 1.05; duration: 4200; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1.05; to: 0.94; duration: 4200; easing.type: Easing.InOutSine }
        }
    }

    // ---- the disk, drawn once flat (seen from above) as a ring with streaks; the GPU turns and squashes it
    component Disk: Canvas {
        property string ck: logo.colorKey; onCkChanged: requestPaint()
        width: logo.diskR * 2
        height: width
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const c = width / 2, ri = logo.shadowR * 1.5, ro = c
            const h = logo.hot, w = logo.warm, k = logo.cool
            const g = ctx.createRadialGradient(c, c, ri, c, c, ro)
            g.addColorStop(0, Qt.rgba(h.r, h.g, h.b, 0.95))
            g.addColorStop(0.18, Qt.rgba(w.r, w.g, w.b, 0.85))
            g.addColorStop(0.55, Qt.rgba(k.r, k.g, k.b, 0.45))
            g.addColorStop(1, Qt.rgba(k.r, k.g, k.b, 0))
            ctx.fillStyle = g
            ctx.beginPath()
            ctx.arc(c, c, ro, 0, Math.PI * 2)
            ctx.arc(c, c, ri, 0, Math.PI * 2, true)
            ctx.fill()
            // streaks: brighter and darker bits of matter (always the same pattern, so the turning shows)
            let seed = 7
            function rnd() { seed = (seed * 16807) % 2147483647; return seed / 2147483647 }
            ctx.lineCap = "round"
            for (let i = 0; i < 46; i++) {
                const r = ri + (ro - ri) * Math.pow(rnd(), 1.4) * 0.9
                const a0 = rnd() * Math.PI * 2, len = 0.4 + rnd() * 1.3
                const bright = rnd() > 0.35
                const t = (r - ri) / (ro - ri)
                ctx.strokeStyle = bright ? Qt.rgba(h.r, h.g, h.b, 0.55 * (1 - t)) : Qt.rgba(0, 0, 0, 0.35 * (1 - t))
                ctx.lineWidth = Math.max(0.8, width * (0.006 + rnd() * 0.012))
                ctx.beginPath()
                ctx.arc(c, c, r, a0, a0 + len)
                ctx.stroke()
            }
        }
    }

    // the disk seen tilted: it turns in its own plane, then it is squashed
    component TiltedDisk: Item {
        width: logo.size
        height: logo.size
        Disk {
            anchors.centerIn: parent
            rotation: logo.angle
            scale: 0.35 + 0.65 * logo.reveal
        }
        transform: Scale { origin.x: logo.size / 2; origin.y: logo.size / 2; yScale: logo.tilt }
    }

    Item {
        id: hole
        anchors.fill: parent
        opacity: Math.min(1, logo.reveal * 1.5)
        // a slow, small float
        property real bob: 0
        SequentialAnimation on bob {
            running: logo.running; loops: Animation.Infinite
            NumberAnimation { from: -0.012; to: 0.012; duration: 3600; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.012; to: -0.012; duration: 3600; easing.type: Easing.InOutSine }
        }
        transform: [
            Translate { y: hole.bob * logo.size },
            Rotation { origin.x: logo.size / 2; origin.y: logo.size / 2; angle: -9 }
        ]

        // 1. the halo: light from the far side of the disk, bent over (and under) the shadow
        Canvas {
        property string ck: logo.colorKey; onCkChanged: requestPaint()
            anchors.fill: parent
            antialiasing: true
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const c = width / 2, r = logo.shadowR * 1.12
                const h = logo.hot, w = logo.warm, k = logo.cool
                for (let i = 0; i < 3; i++) {
                    const rr = r + i * width * 0.022
                    ctx.lineWidth = width * (0.034 - i * 0.008)
                    const g = ctx.createLinearGradient(0, c - rr, 0, c + rr)
                    g.addColorStop(0, Qt.rgba(h.r, h.g, h.b, 0.85 - i * 0.25))
                    g.addColorStop(0.45, Qt.rgba(w.r, w.g, w.b, 0.45 - i * 0.12))
                    g.addColorStop(1, Qt.rgba(k.r, k.g, k.b, 0.35 - i * 0.1))
                    ctx.strokeStyle = g
                    ctx.beginPath()
                    ctx.arc(c, c, rr, 0, Math.PI * 2)
                    ctx.stroke()
                }
            }
            SequentialAnimation on opacity {
                running: logo.running; loops: Animation.Infinite
                NumberAnimation { from: 0.8; to: 1; duration: 2600; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1; to: 0.8; duration: 2600; easing.type: Easing.InOutSine }
            }
        }

        // 2. the whole disk (its far half shows behind the shadow)
        TiltedDisk { }

        // 3. the shadow: nothing comes back from here; 4. the photon ring: its thin, bright edge
        Rectangle {
            anchors.centerIn: parent
            width: logo.shadowR * 2 * (0.2 + 0.8 * logo.reveal)
            height: width
            radius: width / 2
            color: "#000000"
            border.width: Math.max(1, logo.size * 0.012)
            border.color: Qt.rgba(logo.hot.r, logo.hot.g, logo.hot.b, 0.9)
        }

        // 5. the near half of the disk passes in front of the shadow
        Item {
            x: 0; y: logo.size / 2
            width: logo.size; height: logo.size / 2
            clip: true
            TiltedDisk { y: -logo.size / 2 }
        }

        // 6. bits of matter spiralling in
        Repeater {
            model: logo.extras ? 5 : 0
            delegate: Item {
                id: bit
                width: logo.size; height: logo.size
                property real t: index / 5
                NumberAnimation on t {
                    running: logo.running; loops: Animation.Infinite
                    from: index / 5; to: index / 5 + 1; duration: 5200
                }
                readonly property real f: t % 1                      // 0 far out, 1 swallowed
                readonly property real a: (f * 2.4 + index * 1.3) * Math.PI * 2
                readonly property real r: logo.diskR * (0.95 - 0.72 * f)
                Rectangle {
                    width: Math.max(1.5, logo.size * 0.022 * (1 - bit.f * 0.6)); height: width; radius: width / 2
                    color: logo.hot
                    x: logo.size / 2 + Math.cos(bit.a) * bit.r - width / 2
                    y: logo.size / 2 + Math.sin(bit.a) * bit.r * logo.tilt - height / 2
                    // hidden behind the shadow while on the far side, close in
                    visible: !(Math.sin(bit.a) < 0 && Math.hypot(Math.cos(bit.a) * bit.r, Math.sin(bit.a) * bit.r * logo.tilt) < logo.shadowR * 1.1)
                    opacity: logo.reveal * Math.min(1, (1 - bit.f) * 3) * Math.min(1, bit.f * 6)
                }
            }
        }
    }

    // ---- stars
    component Sparkle: Item {
        id: sp
        property real px: 0.5
        property real py: 0.5
        property real r: 0.05
        property real peak: 1
        property int period: 2400
        property int delay: 0
        property int order: 0
        property real tw: 1
        readonly property real popIn: Math.max(0, Math.min(1, (logo.reveal - 0.45 - order * 0.08) / 0.25))
        width: logo.size * r * 2
        height: width
        x: logo.size * px - width / 2
        y: logo.size * py - height / 2
        opacity: sp.peak * sp.tw * sp.popIn
        scale: (0.75 + 0.25 * sp.tw) * (0.3 + 0.7 * sp.popIn)
        Canvas {
        property string ck: logo.colorKey; onCkChanged: requestPaint()
            anchors.fill: parent
            antialiasing: true
            onPaint: {
                const ctx = getContext("2d")
                const w = width, c = w / 2, k = w * 0.11
                ctx.reset()
                ctx.fillStyle = "#ffffff"
                ctx.beginPath()
                ctx.moveTo(c, 0)
                ctx.lineTo(c + k, c - k); ctx.lineTo(w, c); ctx.lineTo(c + k, c + k)
                ctx.lineTo(c, w); ctx.lineTo(c - k, c + k); ctx.lineTo(0, c); ctx.lineTo(c - k, c - k)
                ctx.closePath()
                ctx.fill()
            }
        }
        SequentialAnimation on tw {
            running: logo.running; loops: Animation.Infinite
            PauseAnimation { duration: sp.delay }
            NumberAnimation { from: 1; to: 0.25; duration: sp.period; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.25; to: 1; duration: sp.period; easing.type: Easing.InOutSine }
        }
    }
    Sparkle { visible: logo.extras; px: 0.86; py: 0.16; r: 0.05; period: 1900; delay: 0; order: 0 }
    Sparkle { visible: logo.extras; px: 0.12; py: 0.20; r: 0.036; period: 2600; delay: 500; order: 1; peak: 0.8 }
    Sparkle { visible: logo.extras; px: 0.90; py: 0.82; r: 0.03; period: 2200; delay: 900; order: 2; peak: 0.7 }
    Sparkle { visible: logo.extras; px: 0.10; py: 0.84; r: 0.034; period: 2900; delay: 300; order: 3; peak: 0.7 }
    Sparkle { visible: logo.extras; px: 0.52; py: 0.05; r: 0.024; period: 3100; delay: 700; order: 4; peak: 0.55 }
}
