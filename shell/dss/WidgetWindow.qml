// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Wayland
import QtQuick

// A surface just big enough for the widget (and its shadow), placed where the widget is; only the widget
// rectangle receives input (mask). While you edit the widgets it covers the whole screen, so they can be moved.
// (A full-screen surface for every widget made Hyprland redraw and blur each one over the whole screen: with many
// widgets, laptops stuttered.)
// Outside the editor the widget only takes clicks (calendar arrows, timer, notes...). The editor moves it.
// The widget editor (EditorWindow.qml) changes px/py/sc/fw/fh/op/bgv/sty/flp live and saves on release.
PanelWindow {
    screen: Mon.widgetScreen
    id: w
    property string wid: ""
    property var wc: ({})
    property int defX: 100
    property int defY: 100
    property int cw: 300          // natural size of the content
    property int ch: 200
    property bool free: false     // free width and height (visualizer) instead of scaling
    property int defW: 600
    property int defH: 160
    property int bw: 1920
    property int bh: 1080
    property bool active: true
    property bool draggable: true    // kept for old configs; moving is done in the widget editor
    property bool passive: false  // never takes clicks (visualizer)
    property bool editing: false
    // auto-hide (Settings > Desktop): fades out while windows are open, then the surface goes away
    property bool hidden: false
    property real fk: hidden ? 0 : 1
    // they come back softly (Material 3 emphasized decelerate) and leave a little quicker
    Behavior on fk { NumberAnimation { duration: w.hidden ? 200 : 380; easing.type: Easing.BezierSpline; easing.bezierCurve: w.hidden ? Look.accel : Look.decel } }

    // current look; the editor changes these while you drag
    property real px: defX
    property real py: defY
    property real sc: 1
    property real fw: defW
    property real fh: defH
    property real op: 1
    property real bgv: 1
    property string sty: ""
    property bool flp: false
    property bool dragging: false
    property string vcolor: ""    // visualizer color (accent, gradient, rainbow, mono)
    property bool keys: false     // takes the keyboard when you click it (To-do, Notes)
    property bool typing: false   // a text field in the widget has the keyboard
    // a click anywhere else (the desktop, another window) or Esc ends the typing and gives the keyboard back
    function releaseKeys() { holder.forceActiveFocus() }
    property var grab: null
    Component.onDestruction: if (grab) grab.active = false
    onTypingChanged: if (grab) grab.active = typing && !editing
    // brightness of the wallpaper behind this widget, and the colors that follow from it
    readonly property real lum: Look.lumAt(px, py, dispW, dispH, bw, bh)
    // the card: its own strength (the editor) times the transparency of all widgets (Settings > Widgets)
    readonly property var pal: WPal { wall: w.lum; k: w.bgv * Look.widgetAlpha }
    readonly property real dispW: free ? fw : cw * sc
    readonly property real dispH: free ? fh : ch * sc

    signal moved(string id, int x, int y)
    default property alias content: inner.data

    function adopt() {
        if (dragging) return
        px = wc.x !== undefined ? wc.x : defX
        py = wc.y !== undefined ? wc.y : defY
        sc = wc.scale !== undefined ? wc.scale : 1
        fw = wc.w !== undefined ? wc.w : defW
        fh = wc.h !== undefined ? wc.h : defH
        op = wc.opacity !== undefined ? wc.opacity : 1
        bgv = wc.bg !== undefined ? wc.bg : 1
        sty = wc.style !== undefined ? wc.style : ""
        flp = wc.flip === true
        vcolor = wc.color !== undefined ? wc.color : ""
    }
    onWcChanged: adopt()
    onDefXChanged: adopt()
    onDefYChanged: adopt()
    onDefWChanged: adopt()
    onDefHChanged: adopt()
    Component.onCompleted: {
        adopt()
        // Hyprland tells us when you click outside this widget. Made on the fly, so a Quickshell without Hyprland
        // support only loses this one nicety instead of failing to load the whole shell.
        // (niri has no focus grab: there a click outside ends the typing through the mask instead)
        if (keys && !Wm.niri) {
            try {
                grab = Qt.createQmlObject('import Quickshell.Hyprland\nHyprlandFocusGrab { property var win; windows: [win]; active: false; onCleared: win.releaseKeys() }', w, "focusgrab")
                grab.win = w
            } catch (e) { grab = null }
        }
    }

    visible: active && fk > 0.01
    color: "transparent"
    readonly property bool full: editing
    readonly property int pad: 56                  // room around the widget for its shadow and the way in
    readonly property int mx: full ? 0 : Math.max(0, Math.round(px) - pad)
    readonly property int my: full ? 0 : Math.max(0, Math.round(py) - pad)
    anchors { top: true; left: true; right: w.full; bottom: w.full }
    margins { left: w.mx; top: w.my }
    // never wider or taller than the screen: niri does not show a layer bigger than its output (the visualizer, a
    // full-width strip at the bottom, was missing there), and a widget at the edge needs no room past it
    implicitWidth: w.full ? 0 : Math.max(1, Math.min(bw > 0 ? bw - mx : 1e6, Math.ceil(px - mx + dispW) + pad))
    implicitHeight: w.full ? 0 : Math.max(1, Math.min(bh > 0 ? bh - my : 1e6, Math.ceil(py - my + dispH) + pad))
    exclusionMode: ExclusionMode.Ignore
    // above windows while you edit, so you can see what you change
    WlrLayershell.layer: editing ? WlrLayer.Top : WlrLayer.Bottom
    WlrLayershell.namespace: "rice-widget"
    // To-do and Notes: a click gives them the keyboard (typing), nothing takes it otherwise
    WlrLayershell.keyboardFocus: (keys && !editing) ? WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.None
    // only the widget itself takes clicks; the visualizer (passive) and widgets in the editor take none,
    // so whatever is under them (other widgets, the desktop) stays clickable
    mask: Region { item: (w.passive || w.editing) ? nothing : holder }
    // niri: the blurred wallpaper behind the card (Settings > Widgets > Blur; Hyprland blurs this layer with a rule)
    // only behind the widget's cards (Card.qml): widgets drawn as a shape (the clock, the gauges ...) get no square
    // of blur around them, and a widget made of a few cards gets it behind each card
    readonly property var blurCards: holder.cards.filter(c => c && c.visible && c.color.a > 0.02)
    NiriBlur { win: w; items: w.blurCards; radius: w.pal.radius
        radii: w.blurCards.map(c => c.blurRadius !== undefined ? c.blurRadius : w.pal.radius)
        on: Look.blur && !w.passive && w.bgv * Look.widgetAlpha > 0.05 && w.fk > 0.99 && !w.editing }
    Item { id: nothing; width: 0; height: 0 }

    Item {
        id: holder
        readonly property bool widgetHolder: true
        property var cards: []                    // the Card items inside (they add themselves)
        x: w.px - w.mx
        y: w.py - w.my
        width: w.dispW
        height: w.dispH
        opacity: w.op * w.fk
        scale: 0.92 + 0.08 * w.fk
        transform: Translate { y: (1 - w.fk) * 16 }
        Behavior on opacity { enabled: !w.hidden && w.fk > 0.99; NumberAnimation { duration: 120 } }

        Item {
            id: inner
            width: w.free ? holder.width : w.cw
            height: w.free ? holder.height : w.ch
            scale: w.free ? 1 : w.sc
            transformOrigin: Item.TopLeft
        }

        // a click on a part of the widget that is not a field ends the typing
        MouseArea { anchors.fill: parent; z: -1; onPressed: function(m) { w.releaseKeys(); m.accepted = false } }
        // widgets are clicked, not dragged: moving and resizing happen in the widget editor only
        SfxTap {}
    }
}
