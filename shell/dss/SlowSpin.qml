// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// A slow, endless turn for a decorative shape (the parent), at the widgets' frame rate (Look.fps: 30, 60, 120, or
// 0 = every frame of the screen).
// A RotationAnimator redraws the shape on every frame of the screen (144 times a second on a 144 Hz screen) for a
// turn that takes 30 seconds; the compositor then redraws and blurs that part of the desktop just as often. On
// laptops with integrated graphics many of them together made the whole desktop slow. This looks the same.
Item {
    id: sp
    property bool running: true
    property int period: 30000          // ms for one full turn
    property real turn: 360             // -360 turns the other way
    visible: false
    width: 0; height: 0
    Timer {
        interval: Look.fps > 0 ? Math.round(1000 / Look.fps) : 1000
        repeat: true
        running: sp.running && sp.parent !== null && Look.fps > 0
        onTriggered: sp.parent.rotation = (sp.parent.rotation + sp.turn * interval / sp.period) % 360
    }
    // as fast as the screen: an animator turns it on every frame the screen shows
    RotationAnimator {
        target: sp.parent
        from: sp.parent ? sp.parent.rotation : 0
        to: (sp.parent ? sp.parent.rotation : 0) + sp.turn
        duration: sp.period
        loops: Animation.Infinite
        running: sp.running && sp.parent !== null && Look.fps === 0
    }
}
