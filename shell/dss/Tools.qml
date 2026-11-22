// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick

// The toolbox (SUPER + X, again or Esc closes it): a sheet on the right edge with the small things you reach for
// while you work. Tasks and Note are the same ones as the desktop widgets; Timer is a focus timer that keeps
// counting while the sheet is closed; Convert does units and money; System shows the machine; Chat talks to an AI
// model of your choice. Ctrl + 1 ... 6 or Ctrl + Tab changes the tab. rice-tools open [tab].
// Which edge, how wide, which tabs: Settings > Desktop > Toolbox.
PanelWindow {
    id: tb
    screen: Mon.screen
    property bool shown: false
    // LazyPanel lets a closed panel go after a while: not while the timer runs
    readonly property bool open: shown || timerTab.going

    // ---------- Settings > Desktop > Toolbox (rice-tools set ..., ~/.config/rice/tools.json) ----------
    property var cfg: ({})
    readonly property bool onLeft: cfg.side === "left"
    readonly property real sheetW: Math.max(360, Math.min(560, cfg.width || 440))
    readonly property bool dim: cfg.dim !== false
    readonly property bool outside: cfg.outside !== false
    // how solid the sheet is, and whether what is behind it is blurred (Hyprland: a layer rule, niri: NiriBlur).
    // Hyprland blurs every pixel of this layer that is more solid than the rule's ignore_alpha (0.29): the sheet is
    // (0.3 at least), the dim behind it (0.22) and the shadow are not. V4.80 had the dim above it, so the whole
    // screen was blurred, and the shadow left a lighter square around the sheet
    readonly property real solid: Look.flat ? 1 : Math.max(0.3, Math.min(1, cfg.opacity === undefined ? 0.8 : cfg.opacity))
    readonly property bool blurOn: cfg.blur !== false
    // (V4.80 could fold the sheet into a small island; taken out again in V4.81, the parts stay unused)
    property bool mini: false
    property real mt: 0
    Behavior on mt { NumberAnimation { duration: 460; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }
    onMiniChanged: { mt = mini ? 1 : 0; if (!mini && shown) focusT.restart() }
    Trigger {
        path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/rice/tools.json"
        every: 4000
        onContent: function(t) {
            let j = {}
            try { j = t.trim() !== "" ? JSON.parse(t) : {} } catch (e) { j = {} }
            tb.cfg = j
            if (!tb.shown && typeof j.tab === "string" && tb.order.indexOf(j.tab) >= 0) tb.cur = j.tab
        }
    }
    readonly property var all: [
        { key: "tasks", name: "Tasks", icon: "checklist", glyph: 0xF0135 },
        { key: "note", name: "Note", icon: "sticky_note_2", glyph: 0xF039A },
        { key: "timer", name: "Timer", icon: "timer", glyph: 0xF051B },
        { key: "convert", name: "Convert", icon: "swap_horiz", glyph: 0xF04E1 },
        { key: "system", name: "System", icon: "monitoring", glyph: 0xF0127 }
    ]
    // the tabs you kept, in your order
    readonly property var order: {
        const want = Array.isArray(cfg.tabs) ? cfg.tabs.filter(k => all.some(a => a.key === k)) : []
        return want.length > 0 ? want : all.map(a => a.key)
    }
    readonly property var tabs: order.map(k => all.find(a => a.key === k))
    property string cur: "tasks"
    readonly property int tab: Math.max(0, order.indexOf(cur))
    readonly property var here: tabs[tab] || all[0]
    property int from: 0
    onOrderChanged: if (order.indexOf(cur) < 0) cur = order[0]
    readonly property var pageOf: ({ tasks: tasksTab, note: noteTab, timer: timerTab, convert: convertTab, system: systemTab })
    function go(i) {
        i = (i + order.length) % order.length
        if (i === tab) return
        from = tab; cur = order[i]
        Sfx.play("click")
        focusT.restart()
        if (cfg.remember !== false) keep.restart()
    }
    // it opens next time on the tab you left it on
    Timer { id: keep; interval: 900; onTriggered: DssWin.run(["bash", "-c", "\"$HOME/.local/bin/rice-tools\" set tab \"$1\"", "_", tb.cur]) }
    Timer { id: focusT; interval: 60; onTriggered: { const p = tb.pageOf[tb.cur]; if (p && p.focusIt) p.focusIt(); else keys.forceActiveFocus() } }

    // ---------- opening: like the control center. t runs evenly; every part eases on its own (a quick start and a
    // long soft landing) and comes a moment after the one before it, so the sheet opens as one wave ----------
    property real t: 0
    Behavior on t { NumberAnimation { duration: tb.shown ? (Look.lite ? 280 : 720) : (Look.lite ? 160 : 260); easing.type: Easing.Linear } }
    function ease(p) { return 1 - Math.pow(1 - p, 4) }
    readonly property real et: ease(t)
    function stg(i) { return ease(Math.max(0, Math.min(1, (t - i * 0.05) / 0.6))) }
    onShownChanged: { t = shown ? 1 : 0; if (shown) { mini = false; focusT.restart() } }

    // the sheet and its cards both let the Transparency through (V4.80 made only the sheet see-through, and the
    // cards cover most of it: the slider seemed to do nothing, and so did the blur)
    NPal { id: pal; theme: "shell"; alpha: tb.solid; cardAlpha: Math.min(1, tb.solid + 0.12) }

    Trigger {
        name: "tools-open"; fresh: true
        onFired: function(o) {
            const p = o.trim().split(/\s+/)
            const want = tb.order.indexOf(p[2])
            if (p[1] === "close") tb.shown = false
            else if (p[1] === "open") { if (want >= 0) tb.cur = p[2]; tb.shown = true }
            else if (tb.shown && tb.mini) { if (want >= 0) tb.cur = p[2]; tb.mini = false }
            else if (want >= 0 && tb.shown && want !== tb.tab) tb.go(want)
            else { if (want >= 0) tb.cur = p[2]; tb.shown = !tb.shown }
            if (tb.shown) Sfx.play("click")
        }
    }

    visible: shown || t > 0.01
    color: "transparent"
    anchors { top: true; bottom: true; left: true; right: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-tools"
    WlrLayershell.keyboardFocus: shown && !mini ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    NiriBlur { win: tb; items: [sheet]; radius: sheet.radius; on: tb.blurOn && tb.shown && tb.t > 0.97 && (tb.mt < 0.02 || tb.mt > 0.98) }
    // as an island only the island takes the pointer: the windows under the rest of the screen stay usable
    Item { id: whole; anchors.fill: parent }
    mask: Region { item: tb.mini ? sheet : whole }

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, (tb.dim ? 0.22 : 0) * tb.et * (1 - tb.mt)) }
    MouseArea { anchors.fill: parent; enabled: !tb.mini; onClicked: if (tb.outside) tb.shown = false }

    Shadow { x: sheet.x; y: sheet.y; width: sheet.width; height: sheet.height; radius: sheet.radius; opacity: sheet.opacity; scale: sheet.scale
        transformOrigin: sheet.transformOrigin; on: Look.shadows }
    Rectangle {
        id: sheet
        readonly property real fullW: Math.min(tb.sheetW, tb.width - 24)
        readonly property real fullH: tb.height - 24
        readonly property real miniW: Math.min(fullW, Math.max(250, isle.implicitWidth + 28))
        width: fullW + (miniW - fullW) * tb.mt
        height: fullH + (68 - fullH) * tb.mt
        y: 12
        // it comes out of its edge: a slide, and it grows the last bit
        x: tb.onLeft ? 12 - (1 - tb.et) * 90 : tb.width - width - 12 + (1 - tb.et) * 90
        scale: 0.94 + 0.06 * tb.et
        transformOrigin: tb.onLeft ? Item.Left : Item.Right
        radius: 34
        color: pal.surface
        border.width: 1; border.color: pal.line
        opacity: Math.min(1, tb.et * 1.5)
        clip: true
        // a soft light in your key color at the top, behind the title
        Rectangle {
            width: parent.width * 1.3; height: 280; x: -parent.width * 0.15; y: -150; radius: 140
            opacity: 0.5 * tb.stg(0)
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(pal.primary.r, pal.primary.g, pal.primary.b, 0.0) }
                GradientStop { position: 0.55; color: Qt.rgba(pal.primary.r, pal.primary.g, pal.primary.b, 0.16) }
                GradientStop { position: 1; color: Qt.rgba(pal.primary.r, pal.primary.g, pal.primary.b, 0.0) }
            }
        }
        MouseArea { anchors.fill: parent }

        // ---------------- the island (folded) ----------------
        Item {
            id: isleBox
            width: sheet.miniW; height: 68
            x: tb.onLeft ? 0 : parent.width - width
            opacity: Math.max(0, tb.mt * 2 - 1)
            visible: opacity > 0.01
            MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); tb.mini = false } }
            Row {
                id: isle
                x: 12; anchors.verticalCenter: parent.verticalCenter; spacing: 12
                Rectangle { width: 44; height: 44; radius: 16; color: pal.container; anchors.verticalCenter: parent.verticalCenter
                    MIcon { anchors.centerIn: parent; size: 22; color: pal.inkContainer; name: tb.here.icon; glyph: String.fromCodePoint(tb.here.glyph) } }
                Column { anchors.verticalCenter: parent.verticalCenter
                    Text { text: Tr.t(tb.here.name); color: pal.fg; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Text { readonly property var pg: tb.pageOf[tb.cur]
                        text: pg && pg.summary !== undefined ? pg.summary : ""; visible: text !== ""
                        width: Math.min(implicitWidth, 240); elide: Text.ElideRight
                        color: pal.dim; font.family: Fonts.numbers; font.pixelSize: 12 } }
                ToolBtn { pal: pal; kind: "plain"; h: 36; anchors.verticalCenter: parent.verticalCenter; icon: "close"; glyph: String.fromCodePoint(0xF0156); onClicked: tb.shown = false }
            }
        }

        // keys that nothing inside took
        Item {
            id: keys
            anchors.fill: parent
            focus: true
            opacity: Math.max(0, 1 - tb.mt * 2.2)
            visible: opacity > 0.01
            Keys.onPressed: function(e) {
                const ctrl = e.modifiers & Qt.ControlModifier
                if (e.key === Qt.Key_Escape) tb.shown = false
                else if (ctrl && e.key === Qt.Key_Tab) tb.go(tb.tab + 1)
                else if (ctrl && e.key === Qt.Key_Backtab) tb.go(tb.tab - 1)
                else if (ctrl && e.key >= Qt.Key_1 && e.key <= Qt.Key_6 && e.key - Qt.Key_1 < tb.order.length) tb.go(e.key - Qt.Key_1)
                else return
                e.accepted = true
            }

            // ---------------- the title ----------------
            Item {
                id: head
                x: 24; y: 22; width: parent.width - 48; height: 52
                opacity: tb.stg(0); transform: Translate { y: (1 - tb.stg(0)) * -12 }
                Rectangle {
                    id: badge
                    width: 48; height: 48; radius: 17; color: pal.container; anchors.verticalCenter: parent.verticalCenter
                    rotation: (1 - tb.stg(0)) * -30
                    // the icon turns over when the tab changes
                    MIcon { id: badgeIcon; anchors.centerIn: parent; size: 24; color: pal.inkContainer
                        name: tb.here.icon; glyph: String.fromCodePoint(tb.here.glyph) }
                    SequentialAnimation {
                        id: flip
                        NumberAnimation { target: badge; property: "scale"; to: 0.82; duration: 90; easing.type: Easing.OutCubic }
                        NumberAnimation { target: badge; property: "scale"; to: 1; duration: 340; easing.type: Easing.OutBack; easing.overshoot: 2.4 }
                    }
                }
                Column {
                    id: title
                    anchors.left: badge.right; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 48 - 14 - 48
                    property real slide: 0
                    transform: Translate { x: title.slide }
                    Text { text: Tr.t(tb.here.name); color: pal.fg; font.family: Fonts.main; font.pixelSize: 23; font.weight: Font.DemiBold }
                    // one line about the tab: what is left to do, how long the timer has, how hot the processor is ...
                    Text { width: parent.width; elide: Text.ElideRight; color: pal.dim; font.family: Fonts.main; font.pixelSize: 12
                        readonly property var pg: tb.pageOf[tb.cur]
                        text: pg && pg.summary !== undefined ? pg.summary : ""; visible: text !== "" }
                    ParallelAnimation {
                        id: titleIn
                        NumberAnimation { target: title; property: "opacity"; from: 0; to: 1; duration: 260 }
                        NumberAnimation { target: title; property: "slide"; from: tb.tab > tb.from ? 22 : -22; to: 0; duration: 340; easing.type: Easing.OutCubic }
                    }
                }
                Connections { target: tb; function onCurChanged() { flip.restart(); titleIn.restart() } }
                ToolBtn { pal: pal; kind: "plain"; h: 40; icon: "close"; glyph: String.fromCodePoint(0xF0156)
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; onClicked: tb.shown = false }
            }

            // ---------------- the pages: the one you leave goes to the side it came from ----------------
            Item {
                id: body
                x: 24; y: head.y + head.height + 18
                width: parent.width - 48; height: nav.y - y - 16
                clip: true
                transform: Translate { y: (1 - tb.stg(1)) * 18 }
                component Page: Item {
                    id: pg
                    property string key: ""
                    readonly property int i: tb.order.indexOf(key)
                    readonly property bool cur: tb.cur === key
                    // its parts come in one after another: when the toolbox opens (with it), and again when you go
                    // to this tab
                    property real k: 1
                    readonly property real enter: Math.min(Math.max(0, (tb.t - 0.12) / 0.88), k)
                    NumberAnimation { id: pgIn; target: pg; property: "k"; from: 0; to: 1; duration: 620; easing.type: Easing.Linear }
                    onCurChanged: if (cur && tb.shown && tb.t > 0.99) pgIn.restart()
                    width: body.width; height: body.height
                    opacity: cur ? 1 : 0
                    visible: opacity > 0.01
                    x: cur ? 0 : (i < tb.tab ? -36 : 36)
                    Behavior on opacity { NumberAnimation { duration: cur ? 300 : 140 } }
                    Behavior on x { NumberAnimation { duration: 380; easing.type: Easing.OutCubic } }
                }
                Page { key: "tasks"; ToolTasks { id: tasksTab; anchors.fill: parent; pal: pal; enter: parent.enter; active: tb.shown && tb.cur === "tasks" } }
                Page { key: "note"; ToolNote { id: noteTab; anchors.fill: parent; pal: pal; enter: parent.enter; active: tb.shown && tb.cur === "note" } }
                Page { key: "timer"; ToolTimer { id: timerTab; anchors.fill: parent; pal: pal; enter: parent.enter; active: tb.shown && tb.cur === "timer" } }
                Page { key: "convert"; ToolConvert { id: convertTab; anchors.fill: parent; pal: pal; enter: parent.enter; active: tb.shown && tb.cur === "convert" } }
                Page { key: "system"; ToolSystem { id: systemTab; anchors.fill: parent; pal: pal; enter: parent.enter; active: tb.shown && tb.cur === "system" } }
            }

            // ---------------- the tabs: the one you are on opens into a pill with its name, the others are icons ----------------
            Rectangle {
                id: nav
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height - height - 18
                width: navRow.width + 16; height: 64; radius: 32
                color: pal.card
                border.width: 1; border.color: pal.line
                opacity: tb.stg(4); transform: Translate { y: (1 - tb.stg(4)) * 30 }
                Row {
                    id: navRow
                    x: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                    Repeater {
                        model: tb.tabs
                        delegate: Rectangle {
                            id: nt
                            required property var modelData
                            required property int index
                            readonly property bool cur: tb.tab === index
                            // names are shown while there is room for them (a narrow sheet with six tabs: icons only)
                            readonly property bool wide: cur && sheet.fullW >= 110 + tb.tabs.length * 52
                            height: 48; radius: cur ? 18 : 24
                            width: wide ? ntRow.implicitWidth + 32 : 48
                            Behavior on width { NumberAnimation { duration: 380; easing.type: Easing.OutBack; easing.overshoot: 1.3 } }
                            Behavior on radius { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                            color: cur ? pal.primary : (nm.containsMouse ? pal.cardHi : "transparent")
                            Behavior on color { ColorAnimation { duration: 200 } }
                            // they pop in one after another when the toolbox opens
                            readonly property real pop: tb.stg(4 + nt.index * 0.8)
                            scale: (nm.pressed ? 0.9 : 1) * (0.5 + 0.5 * pop)
                            opacity: pop
                            Behavior on scale { enabled: tb.t > 0.99; NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                            clip: true
                            Row {
                                id: ntRow
                                anchors.centerIn: parent; spacing: 8
                                MIcon { anchors.verticalCenter: parent.verticalCenter; size: 21
                                    name: nt.modelData.icon; glyph: String.fromCodePoint(nt.modelData.glyph)
                                    color: nt.cur ? pal.inkPrimary : (nm.containsMouse ? pal.fg : pal.dim)
                                    Behavior on color { ColorAnimation { duration: 200 } }
                                    // the icon you go to tips over and back
                                    rotation: 0
                                    SequentialAnimation on rotation { id: tip; running: false
                                        NumberAnimation { to: -14; duration: 110; easing.type: Easing.OutCubic }
                                        NumberAnimation { to: 0; duration: 420; easing.type: Easing.OutBack; easing.overshoot: 3 } }
                                    Connections { target: nt; function onCurChanged() { if (nt.cur) tip.restart() } } }
                                Text { visible: nt.wide; anchors.verticalCenter: parent.verticalCenter; text: Tr.t(nt.modelData.name)
                                    color: pal.inkPrimary; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.DemiBold }
                            }
                            // the timer is running: a dot on its tab
                            Rectangle { visible: nt.modelData.key === "timer" && timerTab.going && !nt.cur
                                x: parent.width - 14; y: 8; width: 8; height: 8; radius: 4; color: pal.primary }
                            MouseArea { id: nm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: tb.go(nt.index) }
                        }
                    }
                }
            }
        }
    }
}
