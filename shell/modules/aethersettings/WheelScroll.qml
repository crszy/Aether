// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

// Scrolling a page with the mouse wheel or the touchpad. Put it inside the Flickable:
//   WheelScroll { id: wsc; flick: flick }                 (sideways: horizontal: true)
// and keep the page's own Behavior on contentY for jumps (search) with  enabled: ... && !wsc.active
// The mouse wheel glides to where the notches point; the touchpad moves the page with your fingers, as they go.
// (Before, each wheel step started from where the last glide had got to so far: quick touchpad events added up to
// less than they should, and the page jumped back a little.)
WheelHandler {
    id: ws
    property Flickable flick: null
    property bool horizontal: false
    property real k: Prefs.wheel
    property real goal: 0
    property bool active: false                   // scrolled a moment ago (the page's Behavior stays out of the way)
    property var outer: null                      // a box inside a page: the page's WheelScroll, which scrolls on
                                                  // when the box is at its top or bottom
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    readonly property string prop: horizontal ? "contentX" : "contentY"
    function limit(v) {
        const max = horizontal ? Math.max(0, flick.contentWidth - flick.width) : Math.max(0, flick.contentHeight - flick.height)
        return Math.max(0, Math.min(max, v))
    }
    // smooth: every frame the page covers a part of the way to where the wheel pointed, so several notches in a row
    // blend into one steady, soft run (an animation that restarts on every notch jerks at each one)
    property FrameAnimation glide: FrameAnimation {
        onTriggered: {
            const cur = ws.flick[ws.prop]
            const next = cur + (ws.goal - cur) * (1 - Math.exp(-Math.min(0.05, frameTime) / 0.09))
            if (Math.abs(ws.goal - next) < 0.4) { ws.flick[ws.prop] = ws.goal; stop() }
            else ws.flick[ws.prop] = next
        }
    }
    property Timer rest: Timer { interval: 220; onTriggered: ws.active = false }
    onWheel: function(e) { take(e) }
    function take(e) {
        if (!flick) return
        const cur = flick[prop]
        const touch = e.pixelDelta.x !== 0 || e.pixelDelta.y !== 0
        let to
        if (touch) {
            // a touchpad: follow the fingers
            const d = horizontal ? (e.pixelDelta.x !== 0 ? e.pixelDelta.x : e.pixelDelta.y) : e.pixelDelta.y
            to = limit(cur - d * 1.3 * Math.max(0.4, Prefs.scroll))
        } else {
            const a = horizontal ? (e.angleDelta.x !== 0 ? e.angleDelta.x : e.angleDelta.y) * 1.15 : e.angleDelta.y
            to = limit((glide.running ? goal : cur) - a * k)
        }
        // a box inside a page at its end: the page scrolls instead
        if (outer && Math.abs(to - cur) < 0.5 && !glide.running) { outer.take(e); return }
        active = true
        rest.restart()
        goal = to
        if (touch) { glide.stop(); flick[prop] = goal; return }
        if (!glide.running) glide.start()
    }
}
