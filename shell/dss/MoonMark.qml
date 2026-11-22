// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// The old DarkShadow's Shell logo (for the bar's launcher button), alive: a crescent moon that floats and sways, twinkling stars around it,
// a soft glow and now and then a shooting star. No background, so it sits on anything.
//   size     width and height
//   running  play the animations (pause them when the logo is not on screen)
//   intro    draw itself in when it appears (the moon turns in, then the stars pop up one by one)
Item {
    id: logo
    property real size: 140
    property bool running: visible
    property bool intro: false
    // the stars and the shooting star around it (off for small places, like the bar)
    property bool extras: true
    property color moonTop: "#ffffff"
    property color moonBottom: "#b9bfd8"
    property color glowColor: "#cfd6ff"
    // changes whenever a color changes: the drawings are made again
    readonly property string colorKey: "" + moonTop + moonBottom + glowColor
    // 0 → 1 while it draws in (stays 1 without intro)
    property real reveal: intro ? 0 : 1

    width: size
    height: size

    function replay() { reveal = 0; revealAnim.restart() }
    NumberAnimation {
        id: revealAnim
        target: logo; property: "reveal"; from: 0; to: 1
        duration: 1500; easing.type: Easing.OutCubic
    }
    // the intro plays when the logo first shows up
    Component.onCompleted: if (intro && visible) revealAnim.start()
    onVisibleChanged: if (intro && visible && reveal === 0 && !revealAnim.running) revealAnim.start()

    // ---- soft glow behind the moon
    Canvas {
        property string ck: logo.colorKey; onCkChanged: requestPaint()
        id: glow
        anchors.centerIn: parent
        width: logo.size * 1.25
        height: width
        opacity: 0.85 * logo.reveal
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const g = ctx.createRadialGradient(width / 2, height / 2, 0, width / 2, height / 2, width / 2)
            const c = logo.glowColor
            g.addColorStop(0, Qt.rgba(c.r, c.g, c.b, 0.30))
            g.addColorStop(0.45, Qt.rgba(c.r, c.g, c.b, 0.10))
            g.addColorStop(1, Qt.rgba(c.r, c.g, c.b, 0))
            ctx.fillStyle = g
            ctx.fillRect(0, 0, width, height)
        }
        SequentialAnimation on scale {
            running: logo.running; loops: Animation.Infinite
            NumberAnimation { from: 0.92; to: 1.06; duration: 3800; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1.06; to: 0.92; duration: 3800; easing.type: Easing.InOutSine }
        }
    }

    // ---- the moon (drawn once, then only moved)
    Item {
        id: moonBox
        anchors.fill: parent
        property real bob: 0
        property real sway: 0
        transform: [
            Translate { y: moonBox.bob * logo.size },
            Rotation {
                origin.x: logo.size / 2; origin.y: logo.size / 2
                angle: moonBox.sway - 50 * (1 - logo.reveal)
            }
        ]
        scale: 0.55 + 0.45 * logo.reveal
        opacity: Math.min(1, logo.reveal * 1.6)
        SequentialAnimation on bob {
            running: logo.running; loops: Animation.Infinite
            NumberAnimation { from: -0.018; to: 0.018; duration: 3400; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.018; to: -0.018; duration: 3400; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on sway {
            running: logo.running; loops: Animation.Infinite
            NumberAnimation { from: -5; to: 5; duration: 6200; easing.type: Easing.InOutSine }
            NumberAnimation { from: 5; to: -5; duration: 6200; easing.type: Easing.InOutSine }
        }
        Canvas {
        property string ck: logo.colorKey; onCkChanged: requestPaint()
            id: moon
            anchors.fill: parent
            antialiasing: true
            onPaint: {
                const ctx = getContext("2d")
                const s = width
                ctx.reset()
                const cx = s * 0.5, cy = s * 0.5, R = s * 0.30
                const g = ctx.createLinearGradient(cx - R, cy - R, cx + R, cy + R)
                g.addColorStop(0, logo.moonTop)
                g.addColorStop(1, logo.moonBottom)
                ctx.fillStyle = g
                ctx.beginPath()
                ctx.arc(cx, cy, R, 0, Math.PI * 2)
                ctx.fill()
                // cut the inside away: what stays is the crescent (transparent, no background)
                ctx.globalCompositeOperation = "destination-out"
                ctx.fillStyle = "#000000"
                ctx.beginPath()
                ctx.arc(cx + s * 0.124, cy - s * 0.074, s * 0.258, 0, Math.PI * 2)
                ctx.fill()
                ctx.globalCompositeOperation = "source-over"
            }
        }
    }

    // ---- stars
    component Sparkle: Item {
        id: sp
        property real px: 0.5
        property real py: 0.5
        property real r: 0.05            // radius, part of the logo size
        property real peak: 1
        property int period: 2400
        property int delay: 0
        property int order: 0            // when it pops up during the intro
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
    Sparkle { visible: logo.extras; px: 0.755; py: 0.455; r: 0.062; period: 1900; delay: 0; order: 0 }
    Sparkle { visible: logo.extras; px: 0.86; py: 0.64; r: 0.034; period: 2600; delay: 500; order: 1; peak: 0.8 }
    Sparkle { visible: logo.extras; px: 0.80; py: 0.16; r: 0.045; period: 2200; delay: 900; order: 2; peak: 0.85 }
    Sparkle { visible: logo.extras; px: 0.14; py: 0.22; r: 0.04; period: 2900; delay: 300; order: 3; peak: 0.7 }
    Sparkle { visible: logo.extras; px: 0.12; py: 0.74; r: 0.03; period: 2400; delay: 1200; order: 4; peak: 0.6 }
    Sparkle { visible: logo.extras; px: 0.46; py: 0.07; r: 0.026; period: 3100; delay: 700; order: 5; peak: 0.55 }
    Sparkle { visible: logo.extras; px: 0.56; py: 0.92; r: 0.028; period: 2700; delay: 1500; order: 6; peak: 0.55 }

    // ---- a shooting star now and then
    Item {
        id: shoot
        width: logo.size * 0.34
        height: 2
        rotation: 32
        opacity: 0
        visible: logo.running && logo.extras
        Rectangle {
            anchors.fill: parent
            radius: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0) }
                GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0.9) }
            }
        }
        SequentialAnimation {
            running: logo.running && logo.reveal > 0.99; loops: Animation.Infinite
            PauseAnimation { duration: 5200 }
            ScriptAction { script: { shoot.x = -logo.size * 0.08; shoot.y = -logo.size * 0.04 } }
            ParallelAnimation {
                NumberAnimation { target: shoot; property: "x"; to: logo.size * 0.42; duration: 900; easing.type: Easing.InQuad }
                NumberAnimation { target: shoot; property: "y"; to: logo.size * 0.27; duration: 900; easing.type: Easing.InQuad }
                SequentialAnimation {
                    NumberAnimation { target: shoot; property: "opacity"; to: 0.9; duration: 250 }
                    NumberAnimation { target: shoot; property: "opacity"; to: 0; duration: 650 }
                }
            }
            PauseAnimation { duration: 3600 }
        }
    }
}
