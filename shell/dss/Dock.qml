// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Wayland
import Quickshell.Services.Mpris
import QtQuick
import QtQuick.Effects

// The dock (Settings > Dock), rebuilt in V3.22. Two layer windows:
//   - the dock itself, which only exists while it is shown, at its full size (it never resizes while it moves);
//   - with "Show on hover", a thin invisible strip along the screen edge that brings it back.
// Bottom, left or right edge. Pinned apps from desktop.json, then every open app; the song that plays; apps button.
Scope {
    id: dock
    property var dc: ({})
    property bool pinned: false
    property bool hovering: false
    readonly property bool on: dc.enabled === true
    readonly property bool autoHide: dc.hover !== false
    readonly property bool reveal: on && !Infinite.hideUi && (!autoHide || pinned || hovering || apps.open)
    // Settings > Dock > Keep room for it: while the dock stays out (no hover-to-reveal, or held open with its pin),
    // windows keep away from it, like they do from the bar (an open browser ends above it, not under it)
    readonly property bool reserve: on && dc.reserve !== false && (!autoHide || pinned)
    readonly property string edge: dc.position === "left" || dc.position === "right" ? dc.position : "bottom"
    readonly property bool vert: edge === "left" || edge === "right"
    readonly property real alpha: Look.flat ? 1 : (dc.opacity !== undefined ? Number(dc.opacity) : 0.78)
    // the app icons: in a neutral grey (Tint), in a shade of your wallpaper (Match the wallpaper, the default) or as they come
    readonly property string iconLook: dc.tint === true ? "grey" : (dc.shade !== false ? "shade" : "plain")
    readonly property int icon: 34          // app icon size
    readonly property int cell: 46          // room for one app along the dock
    readonly property int thick: 60         // the dock's width across

    // 0 = hidden, 1 = shown; the window stays until the slide is over
    property real t: reveal ? 1 : 0
    Behavior on t { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

    property bool initDone: false
    onDcChanged: {
        if (!initDone && dc.enabled !== undefined) { pinned = dc.pinnedStartup === true; initDone = true }
    }
    // it only hides when the pointer is really off the dock (a stray "exited" while the dock grows or while an icon
    // is clicked made it hide and come back over and over)
    Timer { id: hideTimer; interval: 700; onTriggered: if (!dockMouse.containsMouse) dock.hovering = false }
    DockApps { id: apps; dock: dock }

    function pickPlayer() {
        const l = Mpris.players.values
        for (let i = 0; i < l.length; i++) if (l[i].isPlaying) return l[i]
        return l.length > 0 ? l[0] : null
    }
    readonly property var player: pickPlayer()
    readonly property bool showMedia: dc.media !== false && player !== null

    function build() {
        const tl = ToplevelManager.toplevels.values
        const out = []
        const used = {}
        const apps = dc.apps || []
        for (let i = 0; i < apps.length; i++) {
            const e = DesktopEntries.heuristicLookup(apps[i])
            if (!e) continue
            const ids = [apps[i].toLowerCase(), (e.id || "").toLowerCase(), (e.startupClass || "").toLowerCase()]
            let t = null
            for (let j = 0; j < tl.length; j++) {
                const id = (tl[j].appId || "").toLowerCase()
                if (id !== "" && ids.indexOf(id) >= 0) { t = tl[j]; break }
            }
            if (t) used[(t.appId || "").toLowerCase()] = true
            out.push({ entry: e, tl: t, key: apps[i] })
        }
        for (let j = 0; j < tl.length; j++) {
            const id = (tl[j].appId || "").toLowerCase()
            if (id === "" || used[id]) continue
            used[id] = true
            out.push({ entry: DesktopEntries.heuristicLookup(tl[j].appId), tl: tl[j], key: id })
        }
        return out
    }
    readonly property var items: build()
    function iconFor(it) {
        const n = it.entry ? it.entry.icon : (it.tl ? (it.tl.appId || "").toLowerCase() : "")
        const p = n !== "" ? Icons.get(n, true) : ""
        return p !== "" ? p : Icons.get("application-x-executable", true)
    }

    // ---------------- the strip that brings the dock back ----------------
    PanelWindow {
        screen: Mon.screen
        id: strip
        visible: dock.on && dock.autoHide && !dock.reveal && dock.t < 0.05
        color: "transparent"
        anchors { bottom: dock.edge === "bottom"; left: dock.edge === "left"; right: dock.edge === "right" }
        implicitWidth: dock.vert ? 3 : Math.max(200, panel.width)
        implicitHeight: dock.vert ? Math.max(200, panel.height) : 3
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Top
        WlrLayershell.namespace: "rice-dock-edge"
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onEntered: { hideTimer.stop(); dock.hovering = true }
        }
    }

    // ---------------- the dock ----------------
    PanelWindow {
        screen: Mon.screen
        id: dockWin
        visible: dock.on && (dock.reveal || dock.t > 0.01)
        color: "transparent"
        anchors { bottom: dock.edge === "bottom"; left: dock.edge === "left"; right: dock.edge === "right" }
        implicitWidth: panel.width + (dock.vert ? 18 : 24)
        implicitHeight: panel.height + (dock.vert ? 24 : 18)
        exclusionMode: dock.reserve ? ExclusionMode.Normal : ExclusionMode.Ignore
        // the panel's width across and its 10 px from the edge
        exclusiveZone: dock.reserve ? dock.thick + 10 : 0
        WlrLayershell.layer: WlrLayer.Top
        WlrLayershell.namespace: "rice-dock"
        // niri: blur behind the dock while it is out (Hyprland blurs this layer with its own rule)
        NiriBlur { win: dockWin; items: [panel]; radius: panel.radius; on: dock.t > 0.95 && dock.dc.background !== false }

        MouseArea {
            id: dockMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            // while it slides away, a window that appears or moves under a still pointer says "entered" by itself:
            // only a real movement of the pointer brings the dock back then
            onEntered: { hideTimer.stop(); if (dock.reveal) dock.hovering = true }
            onPositionChanged: { hideTimer.stop(); dock.hovering = true }
            onExited: if (dock.autoHide) hideTimer.restart()
        }

        Rectangle {
            id: panel
            // sits 10 px from its edge; hidden = slid out over that edge
            readonly property real away: (dock.vert ? width : height) + 14
            x: dock.vert ? (dock.edge === "left" ? 10 : parent.width - width - 10) : (parent.width - width) / 2
            y: dock.vert ? (parent.height - height) / 2 : parent.height - height - 10
            transform: Translate {
                x: dock.edge === "left" ? -panel.away * (1 - dock.t) : (dock.edge === "right" ? panel.away * (1 - dock.t) : 0)
                y: dock.edge === "bottom" ? panel.away * (1 - dock.t) : 0
            }
            opacity: 0.4 + 0.6 * dock.t
            width: dock.vert ? dock.thick : list.implicitWidth + 20
            height: dock.vert ? list.implicitHeight + 20 : dock.thick
            radius: 22
            color: dock.dc.background === false ? "transparent" : Qt.rgba(0.1, 0.1, 0.11, dock.alpha)
            border.width: dock.dc.background === false ? 0 : 1
            border.color: Qt.rgba(1, 1, 1, 0.07)

            Grid {
                id: list
                anchors.centerIn: parent
                spacing: 6
                columns: dock.vert ? 1 : 1000
                horizontalItemAlignment: Grid.AlignHCenter
                verticalItemAlignment: Grid.AlignVCenter

                // keep the dock shown
                Rectangle {
                    visible: dock.dc.pinButton !== false
                    width: 40; height: 40; radius: 14
                    color: dock.pinned ? "#F6F5F4" : (pinM.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.06))
                    Behavior on color { ColorAnimation { duration: 140 } }
                    MIcon { anchors.centerIn: parent; name: "keep"; glyph: String.fromCodePoint(0xF0403); size: 18
                        color: dock.pinned ? "#0d0d0d" : "#F6F5F4"; rotation: dock.pinned ? 0 : 45
                        Behavior on rotation { RotationAnimator { duration: 200 } } }
                    MouseArea { id: pinM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: dock.pinned = !dock.pinned }
                }

                // the apps
                Repeater {
                    model: dock.items
                    delegate: Item {
                        id: app
                        width: dock.vert ? dock.thick - 12 : dock.cell
                        height: dock.vert ? dock.cell : dock.thick - 12
                        readonly property bool running: modelData.tl !== null
                        readonly property bool act: !!modelData.tl && modelData.tl.activated
                        Image {
                            id: ico
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: dock.edge === "left" ? 2 : (dock.edge === "right" ? -2 : 0)
                            anchors.verticalCenterOffset: dock.vert ? 0 : -2
                            width: dock.icon; height: dock.icon
                            source: dock.iconFor(modelData)
                            sourceSize.width: 64; sourceSize.height: 64
                            asynchronous: true
                            smooth: true
                            visible: dock.iconLook === "plain"
                            scale: appM.containsMouse ? 1.16 : 1
                            Behavior on scale { ScaleAnimator { duration: 140; easing.type: Easing.OutCubic } }
                        }
                        // the grey look (Tint) or a shade of the wallpaper (the icon keeps its light and dark parts, in your
                        // theme's color, a little of its own color left in), only drawn when one of them is on
                        Loader {
                            active: dock.iconLook !== "plain"
                            anchors.fill: ico
                            sourceComponent: MultiEffect {
                                source: ico
                                colorization: dock.iconLook === "grey" ? 1.0 : 0.62
                                colorizationColor: dock.iconLook === "grey" ? "#e4e4e7" : Look.accentHi
                                saturation: dock.iconLook === "grey" ? 0 : 0.1
                                Behavior on colorizationColor { ColorAnimation { duration: 600 } }
                                scale: appM.containsMouse ? 1.16 : 1
                                Behavior on scale { ScaleAnimator { duration: 140 } }
                            }
                        }
                        // open: a dot, the focused one a longer line
                        Rectangle {
                            visible: app.running
                            radius: 2
                            color: "#F6F5F4"
                            opacity: app.act ? 1 : 0.5
                            width: dock.vert ? 4 : (app.act ? 14 : 5)
                            height: dock.vert ? (app.act ? 14 : 5) : 4
                            x: dock.edge === "left" ? 0 : (dock.edge === "right" ? parent.width - width : (parent.width - width) / 2)
                            y: dock.vert ? (parent.height - height) / 2 : parent.height - height
                            Behavior on width { NumberAnimation { duration: 160 } }
                            Behavior on height { NumberAnimation { duration: 160 } }
                        }
                        MouseArea {
                            id: appM
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.tl) modelData.tl.activate()
                                else if (modelData.entry) modelData.entry.execute()
                            }
                        }
                    }
                }

                Rectangle {
                    visible: dock.showMedia
                    width: dock.vert ? 30 : 1
                    height: dock.vert ? 1 : 30
                    color: Qt.rgba(1, 1, 1, 0.12)
                }

                // the song that plays
                Grid {
                    visible: dock.showMedia
                    spacing: 8
                    columns: dock.vert ? 1 : 3
                    horizontalItemAlignment: Grid.AlignHCenter
                    verticalItemAlignment: Grid.AlignVCenter
                    RoundImage {
                        width: 38; height: 38; cornerRadius: 12; fallback: ""
                        source: dock.player && dock.player.trackArtUrl ? dock.player.trackArtUrl : ""
                    }
                    Column {
                        visible: dock.dc.nowText !== false && !dock.vert
                        width: 130
                        T { width: parent.width; elide: Text.ElideRight; font.pixelSize: 12; font.bold: true; text: dock.player ? (dock.player.trackTitle || "") : "" }
                        T { width: parent.width; elide: Text.ElideRight; font.pixelSize: 10; color: "#a8a8ad"; text: dock.player ? (dock.player.trackArtist || "") : "" }
                    }
                    Rectangle {
                        width: 34; height: 34; radius: 17
                        color: playM.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.07)
                        Behavior on color { ColorAnimation { duration: 140 } }
                        MIcon { anchors.centerIn: parent; size: 16
                            name: dock.player && dock.player.isPlaying ? "pause" : "play_arrow"
                            glyph: String.fromCodePoint(dock.player && dock.player.isPlaying ? 0xF03E4 : 0xF040A) }
                        MouseArea { id: playM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: if (dock.player) dock.player.togglePlaying() }
                    }
                }

                // all apps (the launcher)
                Rectangle {
                    visible: dock.dc.appsButton !== false
                    width: 40; height: 40; radius: 14
                    color: appsM.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.06)
                    Behavior on color { ColorAnimation { duration: 140 } }
                    MIcon { anchors.centerIn: parent; name: "apps"; glyph: String.fromCodePoint(0xF003B); size: 18; color: "#F6F5F4" }
                    MouseArea { id: appsM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        // the dock's own app menu (Settings > Dock > App menu), else the launcher
                        onClicked: { if (dock.dc.appMenu !== false) apps.open = !apps.open; else DssWin.run(["bash", "-c", "\"$HOME/.local/bin/rice-launch\""]) } }
                }
            }
        }

        // a soft click on the dock's buttons (Settings > Sfx & sounds)
        SfxTap {}
    }
}
