// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// Put it on top of a window: a click on anything clickable there (a hand cursor) makes the click sound.
// Parts that make their own sound (switches, sliders) set "property bool sfxOwn: true" and are skipped.
// It only watches: the click still goes to whatever is under the pointer.
Item {
    id: tap
    property Item root: parent
    anchors.fill: parent
    z: 100000

    function pick(item, x, y, chain) {
        const kids = item.children
        let order = []
        for (let i = 0; i < kids.length; i++) order.push(i)
        order.sort((a, b) => (kids[b].z - kids[a].z) || (b - a))
        for (const i of order) {
            const c = kids[i]
            if (c === tap || !c.visible || c.opacity < 0.05 || !c.enabled) continue
            const p = c.mapFromItem(item, x, y)
            if (p.x < 0 || p.y < 0 || p.x > c.width || p.y > c.height) continue
            chain.push(c)
            pick(c, p.x, p.y, chain)
            return chain
        }
        return chain
    }
    function check(pos) {
        const chain = pick(tap.root, pos.x, pos.y, [])
        let hit = false
        for (const it of chain) {
            if (it.sfxOwn === true) return
            if (it.pressAndHoldInterval !== undefined && it.enabled && it.cursorShape === Qt.PointingHandCursor) hit = true
        }
        if (hit) Sfx.play("click")
    }
    PointHandler {
        acceptedButtons: Qt.LeftButton
        onActiveChanged: if (active) tap.check(point.position)
    }
}
