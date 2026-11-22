// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Wayland
import QtQuick

// Widget editor: pick widgets from the bar at the bottom, drag them, pull a corner to resize,
// change style and opacity under the selected one. Esc or Done to finish.
PanelWindow {
    screen: Mon.widgetScreen
    id: ed
    property var root
    property string sel: ""
    property bool grid: false
    property bool guideX: false
    property bool guideY: false
    readonly property var cat: root ? root.catalog : []
    readonly property var selInfo: { for (let i = 0; i < cat.length; i++) if (cat[i].id === sel) return cat[i]; return null }
    readonly property var selWin: sel !== "" && root && root.wins[sel] ? root.wins[sel] : null
    // a widget you are dragging (or resizing) that comes over the panel on the left: the panel slides away
    readonly property bool panelAway: !!selWin && selWin.dragging && selWin.px < 20 + 300 + 30

    visible: root ? root.editing : false
    color: "transparent"
    anchors { top: true; left: true; right: true; bottom: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-widget-editor"
    WlrLayershell.keyboardFocus: visible ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    onVisibleChanged: { if (visible) { sel = ""; keys.forceActiveFocus() } }

    function info(id) { for (let i = 0; i < cat.length; i++) if (cat[i].id === id) return cat[i]; return null }
    function snap(v, lo, hi, size, guideProp) {
        // magnets: screen edges and center; optional grid
        const m = 10
        if (grid) v = Math.round(v / 16) * 16
        let g = false
        if (Math.abs(v - lo) < m) v = lo
        else if (Math.abs(v + size - hi) < m) v = hi - size
        else if (Math.abs(v + size / 2 - (lo + hi) / 2) < m) { v = (lo + hi) / 2 - size / 2; g = true }
        ed[guideProp] = g
        return Math.max(lo, Math.min(hi - size, v))
    }
    function pick(id) {
        if (!root.enabledOf(id)) root.addWidget(id)
        sel = id
        keys.forceActiveFocus()
    }
    function nudge(dx, dy) {
        const w = selWin
        if (!w) return
        w.px = Math.max(0, Math.min(root.sw - w.dispW, w.px + dx))
        w.py = Math.max(0, Math.min(root.sh - w.dispH, w.py + dy))
        root.saveGeom(sel)
    }

    // ---------- keyboard ----------
    Item {
        id: keys
        focus: true
        Keys.onPressed: function(e) {
            const step = (e.modifiers & Qt.ShiftModifier) ? 10 : 1
            if (e.key === Qt.Key_Escape) { if (ed.sel !== "") ed.sel = ""; else root.finishEditing(); e.accepted = true }
            else if ((e.key === Qt.Key_Delete || e.key === Qt.Key_Backspace) && ed.sel !== "") { root.removeWidget(ed.sel); ed.sel = ""; e.accepted = true }
            else if (e.key === Qt.Key_Left) { ed.nudge(-step, 0); e.accepted = true }
            else if (e.key === Qt.Key_Right) { ed.nudge(step, 0); e.accepted = true }
            else if (e.key === Qt.Key_Up) { ed.nudge(0, -step); e.accepted = true }
            else if (e.key === Qt.Key_Down) { ed.nudge(0, step); e.accepted = true }
            else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) { root.finishEditing(); e.accepted = true }
        }
    }

    // click on empty space: nothing selected
    MouseArea {
        anchors.fill: parent
        onClicked: { ed.sel = ""; keys.forceActiveFocus() }
    }

    // faint guides when a widget sits on the screen's center line
    Rectangle { visible: ed.guideX; x: root ? root.sw / 2 : 0; width: 1; height: parent.height; color: Qt.rgba(1, 1, 1, 0.35) }
    Rectangle { visible: ed.guideY; y: root ? root.sh / 2 : 0; height: 1; width: parent.width; color: Qt.rgba(1, 1, 1, 0.35) }

    // ---------- one frame per widget on the desktop ----------
    Repeater {
        model: ed.root ? ed.root.catalog : []
        delegate: Item {
            id: fr
            readonly property var w: ed.root.wins[modelData.id] || null
            readonly property bool on: !!ed.root && ed.root.enabledOf(modelData.id) && w !== null
            readonly property bool picked: ed.sel === modelData.id
            visible: on
            x: w ? w.px : 0
            y: w ? w.py : 0
            width: w ? w.dispW : 0
            height: w ? w.dispH : 0
            z: picked ? 5 : 1

            // dashed outline
            Canvas {
                anchors.fill: parent
                anchors.margins: -4
                property bool p: fr.picked
                property bool hov: body.containsMouse
                onPChanged: requestPaint()
                onHovChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const c = getContext("2d")
                    c.reset()
                    c.lineWidth = p ? 2 : 1.5
                    c.strokeStyle = p ? "rgba(246,245,244,0.95)" : (hov ? "rgba(246,245,244,0.6)" : "rgba(246,245,244,0.28)")
                    c.setLineDash(p ? [7, 5] : [4, 6])
                    const r = 16, x = 1, y = 1, W = width - 2, H = height - 2
                    c.beginPath()
                    c.moveTo(x + r, y); c.lineTo(x + W - r, y); c.arcTo(x + W, y, x + W, y + r, r)
                    c.lineTo(x + W, y + H - r); c.arcTo(x + W, y + H, x + W - r, y + H, r)
                    c.lineTo(x + r, y + H); c.arcTo(x, y + H, x, y + H - r, r)
                    c.lineTo(x, y + r); c.arcTo(x, y, x + r, y, r)
                    c.closePath()
                    c.stroke()
                }
            }
            Rectangle {
                anchors.fill: parent
                radius: 14
                color: fr.picked ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
            }
            // name tag
            Rectangle {
                visible: !fr.picked
                x: 8; y: 8
                width: tagT.implicitWidth + 14; height: 20; radius: 10
                color: Qt.rgba(0.05, 0.05, 0.06, 0.75)
                T { id: tagT; anchors.centerIn: parent; text: modelData.name; font.pixelSize: 10; color: "#d0d0d4" }
            }

            // move
            MouseArea {
                id: body
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property real sx: 0
                property real sy: 0
                property real ox: 0
                property real oy: 0
                property bool moved: false
                property real tick: 0
                onPressed: function(m) {
                    moved = false
                    tick = 0
                    Sfx.play("select")
                    ed.sel = modelData.id
                    keys.forceActiveFocus()
                    const g = mapToItem(null, m.x, m.y)
                    sx = g.x; sy = g.y; ox = fr.w.px; oy = fr.w.py
                    fr.w.dragging = true
                }
                onPositionChanged: function(m) {
                    if (!pressed) return
                    const g = mapToItem(null, m.x, m.y)
                    if (!moved && Math.abs(g.x - sx) + Math.abs(g.y - sy) < 3) return
                    moved = true
                    const nx = ed.snap(ox + g.x - sx, 0, ed.root.sw, fr.w.dispW, "guideX")
                    const ny = ed.snap(oy + g.y - sy, 0, ed.root.sh, fr.w.dispH, "guideY")
                    tick += Math.abs(nx - fr.w.px) + Math.abs(ny - fr.w.py)
                    if (tick > 34) { tick = 0; Sfx.play("slide") }
                    fr.w.px = nx
                    fr.w.py = ny
                }
                onReleased: {
                    ed.guideX = false; ed.guideY = false
                    fr.w.px = Math.round(fr.w.px); fr.w.py = Math.round(fr.w.py)
                    fr.w.dragging = false
                    if (moved) ed.root.saveGeom(modelData.id)
                }
            }

            // resize handles: corners (and edges for free-size widgets)
            Repeater {
                model: fr.picked ? (modelData.free ? ["tl", "tr", "bl", "br", "t", "b", "l", "r"] : ["tl", "tr", "bl", "br"]) : []
                delegate: Rectangle {
                    readonly property string h: modelData
                    readonly property bool corner: h.length === 2
                    width: corner ? 14 : (h === "t" || h === "b" ? 26 : 8)
                    height: corner ? 14 : (h === "t" || h === "b" ? 8 : 26)
                    radius: corner ? 7 : 4
                    color: "#F6F5F4"
                    border.width: 2
                    border.color: "#101012"
                    x: (h.indexOf("l") >= 0 ? 0 : (h.indexOf("r") >= 0 ? fr.width : fr.width / 2)) - width / 2
                    y: (h.indexOf("t") >= 0 ? 0 : (h.indexOf("b") >= 0 ? fr.height : fr.height / 2)) - height / 2
                    scale: hm.containsMouse || hm.pressed ? 1.25 : 1
                    Behavior on scale { NumberAnimation { duration: 100 } }
                    MouseArea {
                        id: hm
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        cursorShape: parent.h === "tl" || parent.h === "br" ? Qt.SizeFDiagCursor
                                   : parent.h === "tr" || parent.h === "bl" ? Qt.SizeBDiagCursor
                                   : parent.h === "t" || parent.h === "b" ? Qt.SizeVerCursor : Qt.SizeHorCursor
                        property real l0: 0
                        property real t0: 0
                        property real r0: 0
                        property real b0: 0
                        property real tick: 0
                        onPressed: {
                            tick = 0
                            Sfx.play("select")
                            const w = fr.w
                            l0 = w.px; t0 = w.py; r0 = w.px + w.dispW; b0 = w.py + w.dispH
                            w.dragging = true
                        }
                        onPositionChanged: function(m) {
                            if (pressed) { tick += 1; if (tick % 6 === 0) Sfx.play("slide") }
                            if (!pressed) return
                            const w = fr.w, h = parent.h
                            const g = mapToItem(null, m.x, m.y)
                            const SW = ed.root.sw, SH = ed.root.sh
                            if (fr.modelFree) {
                                let l = l0, t = t0, r = r0, b = b0
                                if (h.indexOf("l") >= 0) l = Math.max(0, Math.min(r0 - 120, g.x))
                                if (h.indexOf("r") >= 0) r = Math.min(SW, Math.max(l0 + 120, g.x))
                                if (h.indexOf("t") >= 0) t = Math.max(0, Math.min(b0 - 40, g.y))
                                if (h.indexOf("b") >= 0) b = Math.min(SH, Math.max(t0 + 40, g.y))
                                w.px = l; w.py = t; w.fw = r - l; w.fh = b - t
                            } else {
                                // keep the opposite corner still, keep the shape
                                const ax = h.indexOf("l") >= 0 ? r0 : l0
                                const ay = h.indexOf("t") >= 0 ? b0 : t0
                                let s = Math.max(Math.abs(g.x - ax) / w.cw, Math.abs(g.y - ay) / w.ch)
                                const maxS = Math.min((h.indexOf("l") >= 0 ? ax : SW - ax) / w.cw, (h.indexOf("t") >= 0 ? ay : SH - ay) / w.ch)
                                s = Math.max(0.4, Math.min(3, Math.min(maxS, s)))
                                if (ed.grid) s = Math.round(s * 20) / 20
                                w.sc = s
                                w.px = h.indexOf("l") >= 0 ? ax - w.cw * s : ax
                                w.py = h.indexOf("t") >= 0 ? ay - w.ch * s : ay
                            }
                        }
                        onReleased: {
                            const w = fr.w
                            w.px = Math.round(w.px); w.py = Math.round(w.py)
                            w.fw = Math.round(w.fw); w.fh = Math.round(w.fh)
                            w.dragging = false
                            ed.root.saveGeom(fr.wid)
                        }
                    }
                }
            }
            readonly property bool modelFree: modelData.free === true
            readonly property string wid: modelData.id
        }
    }

    // ---------- toolbar under the selected widget ----------
    Column {
        id: tb
        visible: ed.selWin !== null && !!ed.root && ed.root.enabledOf(ed.sel)
        spacing: 8
        z: 20
        readonly property real fx: ed.selWin ? ed.selWin.px : 0
        readonly property real fy: ed.selWin ? ed.selWin.py : 0
        readonly property real fw: ed.selWin ? ed.selWin.dispW : 0
        readonly property real fh: ed.selWin ? ed.selWin.dispH : 0
        readonly property bool below: fy + fh + 16 + height < ed.height - 60
        x: Math.max(12, Math.min(ed.width - width - 12, fx + fw / 2 - width / 2))
        y: below ? fy + fh + 16 : Math.max(12, fy - height - 16)
        Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6
            EdChip { text: Math.round(tb.fw) + "x" + Math.round(tb.fh) }
            EdChip { text: (tb.fh > 0 ? (tb.fw / tb.fh).toFixed(2) : "1") + ":1" }
            EdChip { visible: ed.selInfo && !ed.selInfo.free; text: Math.round((ed.selWin ? ed.selWin.sc : 1) * 100) + "%" }
            EdIcon {
                icon: ""
                tip: ed.selInfo && ed.selInfo.free ? Tr.t("Full width") : Tr.t("Actual size")
                onClicked: {
                    const w = ed.selWin
                    if (ed.selInfo.free) { w.px = 0; w.fw = ed.root.sw }
                    else { const cx = w.px + w.dispW / 2, cy = w.py + w.dispH / 2; w.sc = 1
                           w.px = Math.max(0, Math.min(ed.root.sw - w.cw, cx - w.cw / 2)); w.py = Math.max(0, Math.min(ed.root.sh - w.ch, cy - w.ch / 2)) }
                    ed.root.saveGeom(ed.sel)
                }
            }
            EdIcon {
                icon: ed.selInfo && ed.selInfo.free ? "" : ""
                tip: ed.selInfo && ed.selInfo.free ? Tr.t("Flip") : Tr.t("Center on screen")
                active: ed.selInfo && ed.selInfo.free ? (ed.selWin ? ed.selWin.flp : false) : false
                onClicked: {
                    const w = ed.selWin
                    if (ed.selInfo.free) { w.flp = !w.flp; ed.root.wset(ed.sel, "flip", w.flp ? "on" : "off") }
                    else { w.px = Math.round((ed.root.sw - w.dispW) / 2); w.py = Math.round((ed.root.sh - w.dispH) / 2); ed.root.saveGeom(ed.sel) }
                }
            }
            EdIcon { icon: ""; tip: Tr.t("Reset"); onClicked: ed.root.resetWidget(ed.sel) }
            EdIcon { icon: ""; tip: Tr.t("Remove"); danger: true; onClicked: { ed.root.removeWidget(ed.sel); ed.sel = "" } }
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6
            EdDrop {
                up: !tb.below
                visible: ed.selInfo && ed.selInfo.styles !== undefined
                options: ed.selInfo && ed.selInfo.styles ? ed.selInfo.styles : []
                current: ed.root ? ed.root.styleOf(ed.sel) : ""
                onChosen: function(v) { ed.root.setStyle(ed.sel, v) }
            }
            EdDrop {
                up: !tb.below
                visible: ed.selInfo && ed.selInfo.opts !== undefined
                options: ed.selInfo && ed.selInfo.opts ? ed.selInfo.opts : []
                current: ed.root && ed.selInfo && ed.selInfo.opts ? ed.root.optOf(ed.sel, ed.selInfo.optKey, ed.selInfo.optDefault) : ""
                onChosen: function(v) { ed.root.setOpt(ed.sel, ed.selInfo.optKey, v) }
            }
            EdSlider {
                icon: ""
                tip: Tr.t("Opacity")
                value: ed.selWin ? ed.selWin.op : 1
                from: 0.1
                onMoved: function(v) { if (ed.selWin) ed.selWin.op = v }
                onDone: function(v) { ed.root.wset(ed.sel, "opacity", v.toFixed(2)) }
            }
            EdSlider {
                visible: ed.selInfo && ed.selInfo.card === true
                icon: ""
                tip: Tr.t("Background")
                value: ed.selWin ? ed.selWin.bgv : 1
                from: 0
                onMoved: function(v) { if (ed.selWin) ed.selWin.bgv = v }
                onDone: function(v) { ed.root.wset(ed.sel, "bg", v.toFixed(2)) }
            }
        }
    }

    // ---------- edit mode: the panel on the left, the way out on the right ----------
    // a calm frame around the screen while you edit
    Rectangle {
        anchors.fill: parent; anchors.margins: 8
        radius: 28
        color: "transparent"
        border.width: 2; border.color: Qt.rgba(1, 1, 1, 0.12 * panel.et)
    }
    EditPanel {
        id: panel
        ed: ed
        x: 20; anchors.verticalCenter: parent.verticalCenter
        // it steps aside while you move or resize a widget over it, and comes back when you let go
        shown: ed.visible && !ed.panelAway
        z: 30
    }
    Rectangle {
        id: exitBtn
        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 22
        width: exitRow.implicitWidth + 32; height: 42; radius: em.pressed ? 14 : 21
        color: em.containsMouse ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(0.06, 0.06, 0.07, 0.85)
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.1)
        opacity: panel.et; transform: Translate { y: (1 - panel.et) * -20 }
        Behavior on color { ColorAnimation { duration: 160 } }
        Behavior on radius { NumberAnimation { duration: 200 } }
        Row {
            id: exitRow; anchors.centerIn: parent; spacing: 8
            MIcon { size: 17; name: "logout"; glyph: "\uf08b"; color: em.containsMouse ? "#0d0d0d" : "#F6F5F4"; anchors.verticalCenter: parent.verticalCenter }
            T { text: Tr.t("Exit edit mode"); font.pixelSize: 13; font.weight: Font.Medium; color: em.containsMouse ? "#0d0d0d" : "#F6F5F4"; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea { id: em; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ed.root.finishEditing() }
    }
    // the keys, once, quietly
    T {
        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 20
        text: Tr.t("Drag to move  ·  pull a corner to resize  ·  arrows nudge  ·  Delete removes  ·  Esc to finish")
        font.pixelSize: 12; color: Qt.rgba(1, 1, 1, 0.55)
        opacity: panel.et
    }

    // ---------- small parts ----------
    component EdChip: Rectangle {
        property string text: ""
        height: 30
        width: ct.implicitWidth + 18
        radius: 9
        color: Qt.rgba(0.06, 0.06, 0.07, 0.92)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
        T { id: ct; kind: "mono"; anchors.centerIn: parent; text: parent.text; font.pixelSize: 11; color: "#e8e8ec" }
    }
    component EdIcon: Rectangle {
        id: ei
        property string icon: ""
        property string tip: ""
        property bool danger: false
        property bool active: false
        property bool big: false
        signal clicked()
        width: big ? 40 : 30
        height: width
        radius: big ? 12 : 9
        color: danger ? (im.containsMouse ? "#e0505a" : "#b8434b")
                      : (active ? "#F6F5F4" : (im.containsMouse ? Qt.rgba(0.16, 0.16, 0.18, 0.95) : Qt.rgba(0.06, 0.06, 0.07, 0.92)))
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
        scale: im.pressed ? 0.9 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        Behavior on color { ColorAnimation { duration: 100 } }
        T { kind: "icon"; anchors.centerIn: parent; text: ei.icon; font.pixelSize: ei.big ? 15 : 13; color: ei.active ? "#0d0d0d" : "#F6F5F4" }
        Rectangle {
            visible: im.containsMouse && ei.tip.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: -height - 6
            width: tt.implicitWidth + 14; height: 22; radius: 7
            color: "#F6F5F4"
            z: 30
            T { id: tt; anchors.centerIn: parent; text: ei.tip; font.pixelSize: 10; font.bold: true; color: "#0d0d0d" }
        }
        MouseArea { id: im; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ei.clicked() }
    }
    component EdSlider: Rectangle {
        id: es
        property string icon: ""
        property string tip: ""
        property real value: 1
        property real from: 0
        property real shown: value
        signal moved(real v)
        signal done(real v)
        onValueChanged: if (!sm.pressed) shown = value
        height: 32
        width: 186
        radius: 10
        color: Qt.rgba(0.06, 0.06, 0.07, 0.92)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
        T { id: si; kind: "icon"; x: 10; anchors.verticalCenter: parent.verticalCenter; text: es.icon; font.pixelSize: 12; color: "#b0b0b6" }
        T { id: sv; kind: "mono"; x: 28; anchors.verticalCenter: parent.verticalCenter; text: Math.round(es.shown * 100) + "%"; font.pixelSize: 11; color: "#e8e8ec"; width: 38 }
        Rectangle {
            id: track
            x: 70; anchors.verticalCenter: parent.verticalCenter
            width: es.width - 82; height: 8; radius: 4
            color: Qt.rgba(1, 1, 1, 0.1)
            Rectangle { width: Math.max(8, parent.width * (es.shown - es.from) / (1 - es.from)); height: parent.height; radius: 4; color: "#F6F5F4" }
            MouseArea {
                id: sm
                anchors.fill: parent
                anchors.margins: -8
                cursorShape: Qt.PointingHandCursor
                function at(mx) { return Math.max(es.from, Math.min(1, es.from + (1 - es.from) * (mx - 8) / track.width)) }
                onPressed: function(m) { es.shown = at(m.x); es.moved(es.shown) }
                onPositionChanged: function(m) { if (pressed) { es.shown = at(m.x); es.moved(es.shown) } }
                onReleased: es.done(es.shown)
            }
        }
        Rectangle {
            visible: (sm.containsMouse || sm.pressed) && es.tip.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height + 6
            width: stt.implicitWidth + 14; height: 22; radius: 7
            color: "#F6F5F4"
            T { id: stt; anchors.centerIn: parent; text: es.tip; font.pixelSize: 10; font.bold: true; color: "#0d0d0d" }
        }
    }
    component EdDrop: Rectangle {
        id: dd
        property var options: []       // [[value, label], ...]
        property string current: ""
        property bool open: false
        property bool up: false         // open the list upwards
        signal chosen(string v)
        function labelOf(v) { for (let i = 0; i < options.length; i++) if (options[i][0] === v) return options[i][1]; return options.length ? options[0][1] : "" }
        height: 32
        width: 140
        radius: 10
        color: dmo.containsMouse || open ? Qt.rgba(0.16, 0.16, 0.18, 0.95) : Qt.rgba(0.06, 0.06, 0.07, 0.92)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
        z: open ? 40 : 0
        T { x: 12; anchors.verticalCenter: parent.verticalCenter; text: dd.labelOf(dd.current); font.pixelSize: 12 }
        T { kind: "icon"; anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: dd.open ? "" : ""; font.pixelSize: 10; color: "#b0b0b6" }
        MouseArea { id: dmo; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: dd.open = !dd.open }
        Rectangle {
            visible: dd.open
            y: dd.up ? -height - 6 : parent.height + 6
            width: parent.width
            height: optCol.implicitHeight + 8
            radius: 10
            color: Qt.rgba(0.08, 0.08, 0.09, 0.97)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.1)
            Column {
                id: optCol
                x: 4; y: 4
                width: parent.width - 8
                Repeater {
                    model: dd.options
                    delegate: Rectangle {
                        width: optCol.width; height: 30; radius: 7
                        color: om.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                        T { x: 10; anchors.verticalCenter: parent.verticalCenter; text: modelData[1]; font.pixelSize: 12; font.bold: modelData[0] === dd.current }
                        T { visible: modelData[0] === dd.current; kind: "icon"; anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; text: ""; font.pixelSize: 10 }
                        MouseArea { id: om; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: { dd.open = false; dd.chosen(modelData[0]) } }
                    }
                }
            }
        }
    }

    // a soft click on every button here (Settings > Sfx & sounds)
    SfxTap { visible: ed.visible }
}
