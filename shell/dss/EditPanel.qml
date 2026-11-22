// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// Edit mode (SUPER + ALT + W): the panel on the left. Sections that open and close:
//   Widgets         every widget as a chip: click to put it on the desktop (and select it), × takes it away
//   Quick settings  blur, transparency, colors from the wallpaper, hiding, grid, the interface font
//   Bar             style, edge, auto-hide and which modules it shows
//   Visualizer      on or off, edge, style, colors and height (cava)
// It slides in from the left, its parts one after another (Material 3 emphasized-decelerate).
Item {
    id: ep
    property var ed: null                   // EditorWindow
    readonly property var root: ed ? ed.root : null
    property bool shown: false
    width: 300
    height: Math.min(parent ? parent.height - 40 : 800, col.implicitHeight + 28)

    property real t: 0
    NumberAnimation { id: tA; target: ep; property: "t"; easing.type: Easing.Linear }
    onShownChanged: { tA.stop(); tA.from = t; tA.to = shown ? 1 : 0; tA.duration = Math.max(1, Math.round((shown ? 680 : 240) * Math.abs(tA.to - t))); tA.start() }
    function ease(p) { return 1 - Math.pow(1 - p, 4) }
    readonly property real et: ease(t)
    function stg(i) { return ease(Math.max(0, Math.min(1, (t - i * 0.06) / 0.7))) }
    opacity: Math.min(1, et * 1.5)
    transform: Translate { x: (1 - ep.et) * -60 }

    NPal { id: pal; theme: "shell"; alpha: 0.93 }
    function ds(args) { DssWin.run([((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/desktop-set"].concat(args.map(function(a) { return String(a) }))) }
    function bar(args) { DssWin.run([((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-bar"].concat(args.map(function(a) { return String(a) }))) }
    function fontSet(f) { DssWin.run(["bash", "-c", "for k in main title numbers reading; do \"$HOME/.local/bin/font-set\" set $k \"$1\" >/dev/null 2>&1; done", "_", f]) }

    // ---------- the bar's settings (bar.json) ----------
    property var bc: ({})
    Trigger {
        path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/rice/bar.json"
        every: 2000
        onContent: function(t) { try { ep.bc = t.trim() !== "" ? JSON.parse(t) : {} } catch (e) { } }
    }
    readonly property var modules: [
        { k: "launcher", n: "Launcher", i: "apps", g: "" }, { k: "workspaces", n: "Workspaces", i: "view_week", g: "" },
        { k: "title", n: "Window", i: "title", g: "" }, { k: "media", n: "Music", i: "music_note", g: "" },
        { k: "tray", n: "Tray", i: "more_horiz", g: "" },
        { k: "volume", n: "Volume", i: "volume_up", g: "" }, { k: "network", n: "Wi-Fi", i: "wifi", g: "" },
        { k: "bluetooth", n: "Bluetooth", i: "bluetooth", g: "" }, { k: "battery", n: "Battery", i: "battery_full", g: "" },
        { k: "notifications", n: "Notifications", i: "notifications", g: "" }, { k: "power", n: "Power", i: "power_settings_new", g: "" },
        { k: "island", n: "Island (clock)", i: "schedule", g: "" }
    ]
    function lay() { return bc.layout ? bc.layout : { left: ["launcher", "workspaces", "title"], center: ["island"], right: ["music", "tray", "volume", "bluetooth", "network", "battery", "notifications", "power"] } }
    function hasMod(k) { const l = lay(); return (l.left || []).indexOf(k) >= 0 || (l.center || []).indexOf(k) >= 0 || (l.right || []).indexOf(k) >= 0 }
    function toggleMod(k) {
        const l = JSON.parse(JSON.stringify(lay()))
        if (hasMod(k)) { ["left", "center", "right"].forEach(function(z) { l[z] = (l[z] || []).filter(function(x) { return x !== k }) }); bar(["layout-set", JSON.stringify({ left: l.left, center: l.center, right: l.right })]) }
        else bar(["layout-add", "right", k])
        const n = JSON.parse(JSON.stringify(bc)); if (hasMod(k)) n.layout = l; else { n.layout = l; n.layout.right = (n.layout.right || []).concat([k]) } bc = n
    }
    function setBar(k, v) { const n = Object.assign({}, bc); n[k] = v; bc = n; bar(["set", k, typeof v === "boolean" ? (v ? "on" : "off") : v]) }

    // ---------- pieces ----------
    component Section: Item {
        id: sec
        property string title: ""
        property string sub: ""
        property string icon: ""
        property string glyph: ""
        property bool open: false
        property int order: 0
        default property alias content: inner.data
        width: col.width
        height: head.height + (open ? inner.implicitHeight + 14 : 0)
        clip: true
        Behavior on height { NumberAnimation { duration: 460; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }
        opacity: ep.stg(sec.order); transform: Translate { x: (1 - ep.stg(sec.order)) * -30 }
        Rectangle { anchors.fill: parent; radius: 22; color: sec.open ? pal.card : (hm.containsMouse ? pal.card : Qt.rgba(1, 1, 1, 0.03))
            Behavior on color { ColorAnimation { duration: 200 } } }
        Item {
            id: head
            width: parent.width; height: 58
            Item {
                x: 12; anchors.verticalCenter: parent.verticalCenter; width: 36; height: 36
                Blob { anchors.fill: parent; kind: sec.open ? Look.shape : "circle"; color: sec.open ? pal.primary : pal.container
                    SlowSpin { turn: 360; period: 30000; running: sec.open && ep.shown && !Look.lite } }
                MIcon { anchors.centerIn: parent; size: 17; name: sec.icon; glyph: sec.glyph; filled: sec.open; color: sec.open ? pal.inkPrimary : pal.inkContainer }
            }
            Column {
                x: 58; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 58 - 40
                Text { text: sec.title; color: pal.fg; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold }
                Text { width: parent.width; elide: Text.ElideRight; text: sec.sub; color: pal.dim; font.family: Fonts.main; font.pixelSize: 11 }
            }
            MIcon { anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter; size: 20; name: "expand_more"; glyph: ""; color: pal.dim
                rotation: sec.open ? 180 : 0; Behavior on rotation { NumberAnimation { duration: 420; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } } }
            MouseArea { id: hm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: sec.open = !sec.open }
        }
        Column {
            id: inner
            x: 12; y: head.height
            width: parent.width - 24
            spacing: 8
            opacity: sec.open ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 300 } }
        }
    }
    component Switchy: Item {
        id: sw
        property string label: ""
        property bool on: false
        signal flipped(bool v)
        width: parent ? parent.width : 200; height: 36
        Text { anchors.verticalCenter: parent.verticalCenter; width: parent.width - 60; elide: Text.ElideRight; text: sw.label; color: pal.fg; font.family: Fonts.main; font.pixelSize: 12 }
        Rectangle {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: 46; height: 28; radius: 14
            color: sw.on ? pal.primary : pal.cardHi
            Behavior on color { ColorAnimation { duration: 180 } }
            Rectangle {
                width: sw.on ? 20 : 14; height: width; radius: width / 2
                anchors.verticalCenter: parent.verticalCenter
                x: sw.on ? parent.width - width - 4 : 7
                color: sw.on ? pal.inkPrimary : pal.dim
                Behavior on x { NumberAnimation { duration: 320; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
                Behavior on width { NumberAnimation { duration: 200 } }
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sw.flipped(!sw.on) }
        }
    }
    component Pills: Flow {
        id: pls
        property var options: []          // [[value, label], ...]
        property string current: ""
        signal chosen(string v)
        width: parent ? parent.width : 200
        spacing: 4
        Repeater {
            model: pls.options
            delegate: Rectangle {
                readonly property bool on: pls.current === modelData[0]
                width: pt.implicitWidth + 22; height: 30
                radius: on ? 15 : 9
                color: on ? pal.primary : (pm.containsMouse ? pal.cardHi : Qt.rgba(1, 1, 1, 0.05))
                Behavior on radius { NumberAnimation { duration: 300; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
                Behavior on color { ColorAnimation { duration: 160 } }
                Text { id: pt; anchors.centerIn: parent; text: modelData[1]; color: parent.on ? pal.inkPrimary : pal.fg; font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.Medium }
                MouseArea { id: pm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: pls.chosen(modelData[0]) }
            }
        }
    }
    component Label: Text { color: pal.dim; font.family: Fonts.main; font.pixelSize: 11; topPadding: 4 }

    // ---------- the panel ----------
    Shadow { anchors.fill: bgr; radius: bgr.radius; on: Look.shadows && Look.shadowPanels }
    Rectangle {
        id: bgr
        anchors.fill: parent
        radius: 28
        color: pal.surface
        border.width: 1; border.color: pal.line
        MouseArea { anchors.fill: parent }     // clicks here do not deselect the widget
    }
    Flickable {
        anchors.fill: parent
        anchors.margins: 14
        contentHeight: col.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Column {
            id: col
            width: parent.width
            spacing: 8

            // ---- header
            Item {
                width: parent.width; height: 52
                opacity: ep.stg(0); transform: Translate { x: (1 - ep.stg(0)) * -30 }
                Item {
                    width: 44; height: 44; anchors.verticalCenter: parent.verticalCenter
                    Blob { anchors.fill: parent; kind: Look.shape; color: pal.primary
                        SlowSpin { turn: 360; period: 24000; running: ep.shown && !Look.lite } }
                    MIcon { anchors.centerIn: parent; size: 20; name: "edit"; glyph: ""; filled: true; color: pal.inkPrimary }
                }
                Column {
                    x: 56; anchors.verticalCenter: parent.verticalCenter
                    Text { text: Tr.t("Edit mode"); color: pal.fg; font.family: Fonts.title; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Text { text: Tr.t("Customize your desktop"); color: pal.dim; font.family: Fonts.main; font.pixelSize: 11 }
                }
                Rectangle {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    width: 40; height: 40; radius: dm.pressed ? 12 : 20
                    color: dm.containsMouse ? pal.primary : pal.card
                    Behavior on radius { NumberAnimation { duration: 200 } }
                    MIcon { anchors.centerIn: parent; size: 20; name: "check"; glyph: ""; color: dm.containsMouse ? pal.inkPrimary : pal.fg }
                    MouseArea { id: dm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ep.root.finishEditing() }
                }
            }

            // ---- widgets
            Section {
                order: 1
                open: true
                icon: "widgets"; glyph: ""; title: Tr.t("Widgets")
                sub: {
                    if (!ep.root) return ""
                    let n = 0; const c = ep.root.catalog
                    for (let i = 0; i < c.length; i++) if (ep.root.enabledOf(c[i].id)) n++
                    return n + " / " + c.length + " active  ·  click to add, × to remove"
                }
                Grid {
                    width: parent.width
                    columns: 2
                    spacing: 6
                    Repeater {
                        model: ep.root ? ep.root.catalog : []
                        delegate: Rectangle {
                            id: wc
                            readonly property bool on: ep.root.enabledOf(modelData.id)
                            readonly property bool picked: ep.ed.sel === modelData.id
                            width: (parent.width - 6) / 2; height: 36
                            radius: wc.on ? 18 : 10
                            color: wc.picked ? pal.primary : (wc.on ? pal.container : (wm.containsMouse ? pal.cardHi : Qt.rgba(1, 1, 1, 0.05)))
                            Behavior on radius { NumberAnimation { duration: 320; easing.type: Easing.OutBack; easing.overshoot: 1.5 } }
                            Behavior on color { ColorAnimation { duration: 180 } }
                            scale: wm.pressed ? 0.95 : 1
                            Behavior on scale { NumberAnimation { duration: 120 } }
                            Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: modelData.icon; font.family: Fonts.icons; font.pixelSize: 13
                                color: wc.picked ? pal.inkPrimary : (wc.on ? pal.inkContainer : pal.dim) }
                            Text { x: 30; width: parent.width - 30 - (wc.on ? 26 : 8); anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
                                text: modelData.name; font.family: Fonts.main; font.pixelSize: 12
                                color: wc.picked ? pal.inkPrimary : (wc.on ? pal.inkContainer : pal.fg) }
                            MouseArea { id: wm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ep.ed.pick(modelData.id) }
                            Rectangle {
                                visible: wc.on
                                anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter
                                width: 22; height: 22; radius: 11
                                color: xm.containsMouse ? Qt.rgba(1, 0.4, 0.4, 0.4) : "transparent"
                                MIcon { anchors.centerIn: parent; size: 13; name: "close"; glyph: ""; color: wc.picked ? pal.inkPrimary : pal.inkContainer }
                                MouseArea { id: xm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: { ep.root.removeWidget(modelData.id); if (ep.ed.sel === modelData.id) ep.ed.sel = "" } }
                            }
                        }
                    }
                }
            }

            // ---- quick settings
            Section {
                order: 2
                icon: "tune"; glyph: ""; title: Tr.t("Quick settings")
                sub: (Look.blur ? Tr.t("Blur") : Tr.t("No blur")) + "  ·  " + Math.round(Look.widgetAlpha * 100) + "%  ·  " + Fonts.main
                Switchy { label: Tr.t("Blur behind widgets"); on: Look.blur; onFlipped: function(v) { Look.blur = v; ep.ds(["widgets-blur", v ? "on" : "off"]) } }
                Label { text: Tr.t("Transparency") + "  ·  " + Math.round((1 - Look.widgetAlpha) * 100) + "%" }
                Pills { options: [["1", "Solid"], ["0.8", "Light"], ["0.6", "Medium"], ["0.4", "Strong"]]
                    current: String(Look.widgetAlpha >= 0.95 ? "1" : (Look.widgetAlpha >= 0.7 ? "0.8" : (Look.widgetAlpha >= 0.5 ? "0.6" : "0.4")))
                    onChosen: function(v) { Look.widgetAlpha = Number(v); ep.ds(["widgets-opacity", v]) } }
                Switchy { label: Tr.t("Colors follow the wallpaper"); on: Look.adapt; onFlipped: function(v) { Look.adapt = v; ep.ds(["widgets-adapt", v ? "on" : "off"]) } }
                Switchy { label: Tr.t("Hide while windows are open"); on: ep.root ? ep.root.cfg.autoHide === true : false; onFlipped: function(v) { ep.ds(["auto-hide", v ? "on" : "off"]) } }
                Switchy { label: Tr.t("Snap to a grid"); on: ep.ed ? ep.ed.grid : false; onFlipped: function(v) { ep.ed.grid = v } }
                Label { text: Tr.t("Font") }
                Pills { options: [["Google Sans Flex", "Google Sans"], ["Rubik", "Rubik"], ["Inter", "Inter"], ["Outfit", "Outfit"]]; current: Fonts.main
                    onChosen: function(v) { ep.fontSet(v) } }
                Item { width: 1; height: 2 }
                Rectangle {
                    width: rt.implicitWidth + 30; height: 34; radius: 17; color: rm.containsMouse ? pal.cardHi : Qt.rgba(1, 1, 1, 0.05)
                    Row { id: rt; anchors.centerIn: parent; spacing: 6
                        MIcon { size: 15; name: "restart_alt"; glyph: ""; color: pal.fg; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: Tr.t("Reset all positions"); color: pal.fg; font.family: Fonts.main; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter } }
                    MouseArea { id: rm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { ep.root.resetAll(); ep.ed.sel = "" } }
                }
            }

            // ---- the bar
            Section {
                order: 3
                icon: "toolbar"; glyph: ""; title: Tr.t("Bar")
                sub: (ep.bc.style || "islands") + "  ·  " + (ep.bc.position || "top") + (ep.bc.autohide === true ? Tr.t("  ·  auto-hide") : "")
                Label { text: Tr.t("Style") }
                Pills { options: [["flat", "Flat"], ["island", "Island"], ["islands", "Islands"], ["pills", "Pills"], ["notch", "Notch"], ["dynamic", "Dynamic"], ["edge", "Edge"]]; current: ep.bc.style === "google" ? "dynamic" : (ep.bc.style || "islands")
                    onChosen: function(v) { ep.setBar("style", v) } }
                Label { text: Tr.t("Edge") }
                Pills { options: [["top", "Top"], ["bottom", "Bottom"], ["left", "Left"], ["right", "Right"]]; current: ep.bc.position || "top"; onChosen: function(v) { ep.setBar("position", v) } }
                Switchy { label: Tr.t("Auto-hide"); on: ep.bc.autohide === true; onFlipped: function(v) { ep.setBar("autohide", v) } }
                Label { text: Tr.t("Modules (click to show or hide)") }
                Grid {
                    width: parent.width
                    columns: 2
                    spacing: 6
                    Repeater {
                        model: ep.modules
                        delegate: Rectangle {
                            readonly property bool on: ep.hasMod(modelData.k)
                            width: (parent.width - 6) / 2; height: 34
                            radius: on ? 17 : 10
                            color: on ? pal.container : (mm.containsMouse ? pal.cardHi : Qt.rgba(1, 1, 1, 0.05))
                            Behavior on radius { NumberAnimation { duration: 300; easing.type: Easing.OutBack; easing.overshoot: 1.5 } }
                            Behavior on color { ColorAnimation { duration: 160 } }
                            MIcon { x: 10; anchors.verticalCenter: parent.verticalCenter; size: 15; name: modelData.i; glyph: modelData.g; filled: parent.on; color: parent.on ? pal.inkContainer : pal.dim }
                            Text { x: 32; width: parent.width - 40; elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter; text: modelData.n
                                color: parent.on ? pal.inkContainer : pal.fg; font.family: Fonts.main; font.pixelSize: 12 }
                            MouseArea { id: mm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ep.toggleMod(modelData.k) }
                        }
                    }
                }
            }

            // ---- the visualizer (cava)
            Section {
                id: viz
                order: 4
                readonly property var w: ep.root && ep.root.wins["visualizer"] ? ep.root.wins["visualizer"] : null
                readonly property bool vOn: ep.root ? ep.root.enabledOf("visualizer") : false
                icon: "graphic_eq"; glyph: ""; title: Tr.t("Visualizer")
                sub: vOn ? ((viz.w && viz.w.flp ? Tr.t("Top") : Tr.t("Bottom")) + "  ·  " + (ep.root ? ep.root.styleOf("visualizer") : "")) : Tr.t("Off")
                Switchy { label: Tr.t("On"); on: viz.vOn; onFlipped: function(v) { if (v) ep.root.addWidget("visualizer"); else ep.root.removeWidget("visualizer") } }
                Label { text: Tr.t("Screen edge") }
                Pills { options: [["bottom", "Bottom"], ["top", "Top"]]; current: viz.w && viz.w.flp ? "top" : "bottom"
                    onChosen: function(v) {
                        const w = viz.w; if (!w) return
                        w.flp = v === "top"; w.py = v === "top" ? 0 : ep.root.sh - w.fh
                        ep.root.wset("visualizer", "flip", w.flp ? "on" : "off"); ep.root.saveGeom("visualizer")
                    } }
                Label { text: Tr.t("Style") }
                Pills { options: [["bars", "Bars"], ["mirror", "Mirror"], ["wave", "Wave"], ["dots", "Dots"]]; current: ep.root ? ep.root.styleOf("visualizer") : "bars"
                    onChosen: function(v) { ep.root.setStyle("visualizer", v) } }
                Label { text: Tr.t("Colors") }
                Pills { options: [["accent", "Theme"], ["gradient", "Gradient"], ["rainbow", "Rainbow"], ["mono", "Mono"]]
                    current: ep.root ? ep.root.optOf("visualizer", "color", "accent") : "accent"
                    onChosen: function(v) { ep.root.setOpt("visualizer", "color", v) } }
                Label { text: Tr.t("Bars") }
                Pills { options: [["16", "16"], ["32", "32"], ["48", "48"], ["64", "64"]]
                    current: ep.root ? String(ep.root.optOf("visualizer", "bars", 64)) : "64"
                    onChosen: function(v) { ep.root.setOpt("visualizer", "bars", v) } }
                Label { text: Tr.t("How high they go") }
                Pills { options: [["1", "Calm"], ["1.4", "Normal"], ["2", "High"], ["2.8", "Max"]]
                    current: ep.root ? String(ep.root.optOf("visualizer", "gain", "1.4")) : "1.4"
                    onChosen: function(v) { ep.root.setOpt("visualizer", "gain", v) } }
                Label { text: Tr.t("Bar ends") }
                Pills { options: [["square", "Square"], ["rounded", "Rounded"], ["pill", "Pill"]]
                    current: ep.root ? ep.root.optOf("visualizer", "shape", "square") : "square"
                    onChosen: function(v) { ep.root.setOpt("visualizer", "shape", v) } }
                Label { text: Tr.t("Space between") }
                Pills { options: [["thin", "Thin"], ["normal", "Normal"], ["wide", "Wide"]]
                    current: ep.root ? ep.root.optOf("visualizer", "gap", "normal") : "normal"
                    onChosen: function(v) { ep.root.setOpt("visualizer", "gap", v) } }
                Label { text: Tr.t("Height  ·  ") + Math.round(viz.w ? viz.w.fh : 180) + Tr.t(" px") }
                Item {
                    width: parent.width; height: 30
                    readonly property real f: viz.w ? Math.max(0, Math.min(1, (viz.w.fh - 60) / 640)) : 0.2
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 10; radius: 5; color: pal.cardHi }
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: Math.max(10, parent.width * parent.f); height: 10; radius: 5; color: pal.primary }
                    Rectangle { x: parent.width * parent.f - 2; anchors.verticalCenter: parent.verticalCenter; width: 4; height: 24; radius: 2; color: pal.primary }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        function set(x) {
                            const w = viz.w; if (!w) return
                            const h = Math.round(60 + Math.max(0, Math.min(1, x / width)) * 640)
                            w.fh = h; if (!w.flp) w.py = ep.root.sh - h
                        }
                        onPressed: function(m) { set(m.x) }
                        onPositionChanged: function(m) { if (pressed) set(m.x) }
                        onReleased: ep.root.saveGeom("visualizer")
                    }
                }
            }
        }
    }
}
