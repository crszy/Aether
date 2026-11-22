// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

// One settings page. Material 3 "shared axis": the page you leave slips away a little in the direction you go
// and fades out fast, the new one comes in from the other side on the emphasized-decelerate curve.
// Tabs of a group move sideways, groups of the rail move up or down.
Item {
    id: slot
    property int idx: 0
    property int cur: 0
    property var nav: null            // the window: navX / navY tell the direction
    readonly property bool on: idx === cur
    default property alias content: inner.data
    // the page itself, made when you first go there or a moment after the window opened (one after another), so
    // Settings opens fast: making every page at once (and starting all their helpers) took seconds on a laptop
    property Component page: null
    // made when you first go there, or when you search (search looks inside every page); never all at once at the
    // start (that made Settings slow to open, most of all on laptops)
    readonly property bool warm: !!(nav && nav.searchWanted)
    property bool seen: false

    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    visible: inner.opacity > 0.01
    enabled: on
    z: on ? 1 : 0

    readonly property var decel: [0.05, 0.7, 0.1, 1, 1, 1]
    readonly property var accel: [0.3, 0, 0.8, 0.15, 1, 1]
    readonly property real dx: nav ? nav.navX * 56 : 0
    readonly property real dy: nav ? nav.navY * 40 : 24

    onOnChanged: {
        outAnim.stop(); inAnim.stop()
        if (on) {
            seen = true
            inner.x = slot.dx; inner.y = slot.dy; inner.opacity = 0; inner.scale = 0.985
            inAnim.start()
        } else {
            outX.to = -slot.dx * 0.5; outY.to = -slot.dy * 0.5
            outAnim.start()
        }
    }
    Component.onCompleted: { inner.opacity = on ? 1 : 0; if (on) seen = true }

    ParallelAnimation {
        id: inAnim
        SequentialAnimation {
            PauseAnimation { duration: M3.d(70) }
            NumberAnimation { target: inner; property: "opacity"; to: 1; duration: M3.d(260); easing.type: Easing.BezierSpline; easing.bezierCurve: slot.decel }
        }
        NumberAnimation { target: inner; property: "x"; to: 0; duration: M3.d(480); easing.type: Easing.BezierSpline; easing.bezierCurve: slot.decel }
        NumberAnimation { target: inner; property: "y"; to: 0; duration: M3.d(480); easing.type: Easing.BezierSpline; easing.bezierCurve: slot.decel }
        NumberAnimation { target: inner; property: "scale"; to: 1; duration: M3.d(480); easing.type: Easing.BezierSpline; easing.bezierCurve: slot.decel }
    }
    ParallelAnimation {
        id: outAnim
        NumberAnimation { target: inner; property: "opacity"; to: 0; duration: M3.d(110); easing.type: Easing.BezierSpline; easing.bezierCurve: slot.accel }
        NumberAnimation { id: outX; target: inner; property: "x"; duration: M3.d(160); easing.type: Easing.BezierSpline; easing.bezierCurve: slot.accel }
        NumberAnimation { id: outY; target: inner; property: "y"; duration: M3.d(160); easing.type: Easing.BezierSpline; easing.bezierCurve: slot.accel }
    }

    Item {
        id: inner
        width: parent.width
        height: parent.height
        transformOrigin: Item.Top
        Loader {
            anchors.fill: parent
            active: slot.page !== null && (slot.on || slot.seen || slot.warm)
            asynchronous: !slot.on
            sourceComponent: slot.page
            onLoaded: if (slot.nav && slot.nav.pageLoaded) slot.nav.pageLoaded()
        }
    }
}
