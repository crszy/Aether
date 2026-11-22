// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Wayland
import Quickshell.Widgets
import QtQuick

// The dock's app menu (the apps button on the dock): every app, a search, and a pin on each one to keep your
// favourites on the dock (Settings > Dock > Pinned apps does the same). It grows out of the dock.
PanelWindow {
    screen: Mon.screen
    id: am
    property var dock: null
    property bool open: false
    property real t: 0
    Behavior on t { NumberAnimation { duration: am.open ? 560 : 200; easing.type: Easing.Linear } }
    readonly property real et: 1 - Math.pow(1 - t, 4)
    onOpenChanged: { t = open ? 1 : 0; if (open) { search.text = ""; search.forceActiveFocus() } }
    function stg(i) { return 1 - Math.pow(1 - Math.max(0, Math.min(1, (t - 0.1 - Math.min(i, 20) * 0.02) / 0.5)), 4) }

    readonly property string edge: dock ? dock.edge : "bottom"
    readonly property var pinnedIds: dock && dock.dc.apps ? dock.dc.apps : []
    NPal { id: pal; theme: "shell"; alpha: dock ? Math.min(1, dock.alpha + 0.12) : 0.9 }

    function idOf(e) { return (e.id || "").replace(/\.desktop$/, "") }
    function isPinned(e) { const i = idOf(e); return pinnedIds.indexOf(i) >= 0 }
    function togglePin(e) {
        const i = idOf(e)
        const pinned = isPinned(e)
        DssWin.run(["bash", "-c", "\"$HOME/.local/bin/desktop-set\" \"$1\" \"$2\"", "_", pinned ? "dock-unpin" : "dock-pin", i])
        // show it at once; desktop.json brings the same list back in a moment
        if (dock) {
            const n = JSON.parse(JSON.stringify(dock.dc))
            n.apps = pinned ? (n.apps || []).filter(function(x) { return x !== i }) : (n.apps || []).concat([i])
            dock.dc = n
        }
    }
    readonly property var apps: {
        const l = DesktopEntries.applications.values.filter(function(e) { return !e.noDisplay })
        const q = search.text.trim().toLowerCase()
        const f = q === "" ? l : l.filter(function(e) {
            return (e.name || "").toLowerCase().indexOf(q) >= 0 || (e.genericName || "").toLowerCase().indexOf(q) >= 0
                || (e.keywords || []).join(" ").toLowerCase().indexOf(q) >= 0
        })
        return f.slice().sort(function(a, b) {
            const pa = am.isPinned(a) ? 0 : 1, pb = am.isPinned(b) ? 0 : 1
            return pa !== pb ? pa - pb : (a.name || "").localeCompare(b.name || "")
        })
    }
    function launch(e) { e.execute(); open = false }

    visible: open || (t > 0.01 && !closeT.done)
    // if the closing animation ever stalls (the compositor sent no frames for a moment), it still goes away
    Timer { id: closeT; property bool done: false; interval: 600; running: !open; onRunningChanged: if (running) done = false; onTriggered: done = true }
    // niri: blur behind the card (Hyprland blurs this layer with its own rule)
    NiriBlur { win: am; items: [panel]; radius: panel.radius; on: am.open && am.et > 0.94 }
    color: "transparent"
    anchors { top: true; left: true; right: true; bottom: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-dock-apps"
    WlrLayershell.keyboardFocus: open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    MouseArea { anchors.fill: parent; onClicked: am.open = false }

    Shadow { x: panel.x; y: panel.y; width: panel.width; height: panel.height; radius: panel.radius; opacity: panel.opacity; scale: panel.scale
        transformOrigin: panel.transformOrigin; on: Look.shadows && Look.shadowPanels }
    Rectangle {
        id: panel
        width: Math.min(parent.width - 40, 640)
        height: Math.min(parent.height - 140, 560)
        x: am.edge === "left" ? 92 : (am.edge === "right" ? parent.width - width - 92 : (parent.width - width) / 2)
        y: am.edge === "bottom" ? parent.height - height - 92 : (parent.height - height) / 2
        radius: 32
        color: pal.surface
        border.width: 1; border.color: pal.line
        opacity: Math.min(1, am.et * 1.5)
        scale: 0.9 + 0.1 * am.et
        transformOrigin: am.edge === "left" ? Item.Left : (am.edge === "right" ? Item.Right : Item.Bottom)
        transform: Translate { y: am.edge === "bottom" ? (1 - am.et) * 40 : 0; x: am.edge === "left" ? (1 - am.et) * -40 : (am.edge === "right" ? (1 - am.et) * 40 : 0) }
        clip: true
        MouseArea { anchors.fill: parent }

        // search
        Rectangle {
            id: sbox
            x: 18; y: 18; width: parent.width - 36; height: 48; radius: 24
            color: pal.card
            MIcon { x: 16; anchors.verticalCenter: parent.verticalCenter; size: 20; name: "search"; glyph: "\uf002"; color: pal.dim }
            TextInput {
                id: search
                x: 48; width: parent.width - 64; anchors.verticalCenter: parent.verticalCenter
                color: pal.fg; font.family: Fonts.main; font.pixelSize: 15
                selectionColor: pal.primary; selectedTextColor: pal.inkPrimary
                clip: true
                Keys.onEscapePressed: am.open = false
                Keys.onReturnPressed: if (am.apps.length > 0) am.launch(am.apps[0])
                Text { visible: search.text === ""; text: Tr.t("Search apps"); color: pal.dim; font: search.font }
            }
        }
        Text { x: 22; y: sbox.y + sbox.height + 12; text: am.pinnedIds.length + Tr.t(" pinned  ·  ") + am.apps.length + Tr.t(" apps  ·  the pin keeps one on the dock")
            color: pal.dim; font.family: Fonts.main; font.pixelSize: 11 }

        GridView {
            id: grid
            x: 12; y: sbox.y + sbox.height + 34
            width: parent.width - 24; height: parent.height - y - 12
            cellWidth: width / Math.max(4, Math.floor(width / 120)); cellHeight: 112
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: am.apps
            delegate: Item {
                id: cellIt
                width: grid.cellWidth; height: grid.cellHeight
                readonly property bool pin: am.isPinned(modelData)
                opacity: am.stg(index); transform: Translate { y: (1 - am.stg(index)) * 18 }
                Rectangle {
                    anchors.fill: parent; anchors.margins: 4; radius: 22
                    color: cm.containsMouse ? pal.card : "transparent"
                    Behavior on color { ColorAnimation { duration: 140 } }
                }
                Item {
                    anchors.horizontalCenter: parent.horizontalCenter; y: 14
                    width: 56; height: 56
                    Blob { anchors.fill: parent; kind: Look.shape; color: cellIt.pin ? pal.container : "transparent"; visible: cellIt.pin }
                    IconImage { anchors.centerIn: parent; implicitSize: 40; source: Icons.get(modelData.icon || "application-x-executable", "application-x-executable")
                        scale: cm.pressed ? 0.9 : (cm.containsMouse ? 1.08 : 1)
                        Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack; easing.overshoot: 1.6 } } }
                }
                Text { anchors.horizontalCenter: parent.horizontalCenter; y: 76; width: parent.width - 14; horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight; text: modelData.name; color: pal.fg; font.family: Fonts.main; font.pixelSize: 12 }
                MouseArea { id: cm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(m) { if (m.button === Qt.RightButton) am.togglePin(modelData); else am.launch(modelData) } }
                // the pin
                Rectangle {
                    anchors.right: parent.right; anchors.rightMargin: 10; y: 10
                    width: 28; height: 28; radius: 14
                    visible: cellIt.pin || cm.containsMouse || pm.containsMouse
                    color: cellIt.pin ? pal.primary : (pm.containsMouse ? pal.cardHi : pal.card)
                    MIcon { anchors.centerIn: parent; size: 15; name: "keep"; glyph: "\uf08d"; filled: cellIt.pin; color: cellIt.pin ? pal.inkPrimary : pal.fg }
                    MouseArea { id: pm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: am.togglePin(modelData) }
                }
            }
        }
    }
}
