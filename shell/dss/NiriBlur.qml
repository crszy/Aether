// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import QtQuick

// Blur behind parts of a window on niri (26.04 and newer): the window asks niri to blur exactly behind `items`
// (rounded by `radius`, or one radius per item in `radii`), through Quickshell's BackgroundEffect
// (ext-background-effect, Quickshell 0.3 and newer). Put it straight into the window (not inside another item).
// Hyprland blurs the shell's layers with its own rules, so this does nothing there; an older Quickshell has no
// BackgroundEffect: then there is simply no blur, nothing breaks.
//   NiriBlur { win: myWindow; items: [card]; radius: 20; on: myWindow.et > 0.97 }
//
// The blur follows copies of the items placed by their plain positions (x, y of the item and of the items it
// sits in), not by where an animation (scale, Translate) shows them for a moment: a region read in the middle of
// an opening animation stayed there afterwards, a few pixels off (a pale band above menus, around the bar).
// So `on` should be true only once the part has settled.
Item {
    id: nb
    property var win: null
    property var items: []
    property real radius: 0
    property var radii: []
    property bool on: true
    readonly property bool want: Wm.niri && on && !Look.flat && win !== null && !!items && items.length > 0
    x: 0; y: 0; width: 0; height: 0
    visible: true
    enabled: false

    // where an item is in the window: its plain position, and the scale of items that grow from their top left
    // corner (a widget's size); other scales and Translates are animations, which are left out
    function geo(t) {
        let x = 0, y = 0, k = 1
        for (let p = t; p && p !== nb.parent; p = p.parent) {
            const s = (p.transformOrigin === Item.TopLeft && p.scale > 0) ? p.scale : 1
            x = p.x + x * s; y = p.y + y * s; k *= s
        }
        return { x: x, y: y, k: k }
    }
    readonly property int count: Math.min(8, items ? items.length : 0)
    readonly property int steps: 6
    Repeater {
        id: mirrors
        model: nb.count
        delegate: Item {
            required property int index
            readonly property var src: nb.items && nb.items.length > index ? nb.items[index] : null
            readonly property var g: src ? nb.geo(src) : ({ x: 0, y: 0, k: 1 })
            readonly property real k: g.k
            x: g.x
            y: g.y
            width: src && src.visible !== false ? src.width * g.k : 0
            height: src ? src.height * g.k : 0
            // the rounded corners as a few steps that stay inside the curve: rounded regions came out square on
            // some setups (pale squares at the corners of cards and menus), steps inside the corner never show
            readonly property real rr: Math.min(width / 2, height / 2, ((nb.radii || []).length > index ? nb.radii[index] : nb.radius) * k)
            Repeater {
                id: steps
                model: nb.steps
                delegate: Item {
                    required property int index
                    readonly property real th: index / (nb.steps - 1) * Math.PI / 2
                    readonly property real dx: Math.round(parent.rr * (1 - Math.cos(th)))
                    readonly property real dy: Math.round(parent.rr * (1 - Math.sin(th)))
                    x: dx; y: dy
                    width: Math.max(0, parent.width - 2 * dx); height: Math.max(0, parent.height - 2 * dy)
                }
            }
            function step(j) { return steps.itemAt(j) }
        }
    }

    property var helper: null
    property int made: -1          // for how many items the helper was made

    function rebuild() {
        if (!Wm.niri || win === null) return
        const n = count
        if (helper && made === n) return
        if (helper) { try { helper.clear(); helper.destroy() } catch (e) { } helper = null }
        let regs = ""
        const m = n * nb.steps
        for (let i = 0; i < m; i++)
            regs += "Region { item: h.want && h.boxes.length > " + i + " ? h.boxes[" + i + "] : null }\n"
        try {
            helper = Qt.createQmlObject(
                "import Quickshell\nimport Quickshell.Wayland\nimport QtQuick\n" +
                "QtObject { id: h\n property var win: null\n property var boxes: []\n property bool want: false\n" +
                " property var reg: Region {\n" + regs + " }\n" +
                " function apply() { if (!win) return true; try { win.BackgroundEffect.blurRegion = reg; return true } catch (e) { return false } }\n" +
                " function clear() { try { if (win) win.BackgroundEffect.blurRegion = null } catch (e) { } }\n}", nb, "niriblur")
        } catch (e) { helper = null; made = -2; return }      // this Quickshell cannot blur
        made = n
        helper.win = Qt.binding(() => nb.win)
        helper.boxes = Qt.binding(() => { const l = []; for (let i = 0; i < mirrors.count; i++) { const it = mirrors.itemAt(i); for (let j = 0; j < nb.steps; j++) l.push(it ? it.step(j) : null) } return l })
        helper.want = Qt.binding(() => nb.want)
        if (!helper.apply()) { helper.destroy(); helper = null; made = -2 }      // no BackgroundEffect here
    }
    // a window that was hidden (the launcher, menus) is a new surface for niri when it shows again: the blur
    // region is sent again, or the window came back without blur
    Connections {
        target: nb.win
        ignoreUnknownSignals: true
        function onVisibleChanged() { if (nb.win && nb.win.visible && nb.helper) Qt.callLater(nb.reapply) }
    }
    onWantChanged: if (want && helper) Qt.callLater(reapply)
    function reapply() { if (!helper) return; try { helper.clear() } catch (e) { } if (!helper.apply()) { helper.destroy(); helper = null; made = -2 } }
    onWinChanged: if (made !== -2) Qt.callLater(rebuild)
    onCountChanged: if (made !== -2) Qt.callLater(rebuild)
    Component.onCompleted: if (made !== -2) Qt.callLater(rebuild)
}
