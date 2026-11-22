// Aether shell (AGPL-3.0-or-later). Every widget the Aether bar can show, picked by `wid` (ids in AetherWidgets.js).
// The ideas come from Ryoku's rail widgets (RailWorkspaces, RailClock, RailMusic, RailSysmon, RailTray, RailStatus),
// end-4's bar (active window, weather, media buttons), serpantinum's date and DarkShadowShell's keyboard chip;
// the code is Aether's own, on Aether's services.
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Widgets
import Quickshell.Services.SystemTray
import Quickshell.Services.UPower
import Quickshell.Windows as Win
import Caelestia.Config
import Caelestia.Services
import Caelestia.State
import qs.components
import qs.services
import qs.utils
import qs.modules.aether as Ae

Loader {
    id: w

    required property string wid
    property var screen: null
    property real barH: 38

    readonly property string fontFamily: Tokens.font.body.small.family
    readonly property var st: screen ? ShellState.forScreen(screen) : null

    sourceComponent: {
        switch (wid) {
        case "logo": return logoC;
        case "workspaces": return wsC;
        case "clock": return clockC;
        case "date": return dateC;
        case "music": return musicC;
        case "media": return mediaC;
        case "sysmon": return sysC;
        case "tray": return trayC;
        case "volume": return volC;
        case "network": return netC;
        case "battery": return batC;
        case "notifs": return notifC;
        case "power": return powerC;
        case "window": return winC;
        case "weather": return weatherC;
        case "keyboard": return kbC;
        case "spacer": return spacerC;
        }
        return null;
    }

    component Label: Text {
        color: AetherStyle.textMain
        font.family: w.fontFamily
        font.pixelSize: 13
        font.weight: Font.Medium
        verticalAlignment: Text.AlignVCenter
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
    }
    component Sym: MaterialIcon {
        color: AetherStyle.textMain
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
    }

    // ---- Aether logo: the dashboard ----
    Component {
        id: logoC
        Chip {
            barH: w.barH
            pad: 9
            active: AetherBarConfig.dashOpen && AetherBarConfig.dashTab === "home"
            onClicked: AetherBarConfig.toggleDash("home", w.screen)
            Ae.AetherLogo {
                size: 18
                arcColour: AetherStyle.primary
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ---- workspaces (Ryoku): a pill slides to the one you are on, filled dots have windows ----
    Component {
        id: wsC
        Chip {
            id: wsChip
            barH: w.barH
            hoverable: false
            pad: 6
            readonly property var occ: {
                const o = {};
                const l = Hypr.workspaces.values;
                for (let i = 0; i < l.length; i++)
                    if (l[i].id > 0 && l[i].toplevels.values.length > 0)
                        o[l[i].id] = true;
                return o;
            }
            // each monitor has its own group of workspaces (1-N, N+1-2N... as Caelestia's bar): show this screen's group
            readonly property var mon: w.screen ? Hypr.monitorFor(w.screen) : null
            readonly property int activeId: mon && mon.activeWorkspace ? mon.activeWorkspace.id : Hypr.activeWsId
            readonly property int count: Math.max(1, AetherBarConfig.workspaces)
            readonly property int base: Math.floor(Math.max(0, activeId - 1) / count) * count
            readonly property real dot: w.barH - 18
            onWheel: e => {
                const next = wsChip.activeId + (e.angleDelta.y < 0 ? 1 : -1);
                if (next > wsChip.base && next <= wsChip.base + wsChip.count)
                    Hypr.focusWorkspace(next);
            }
            Item {
                width: wsRow.width
                height: wsChip.dot
                Rectangle {   // the active pill
                    readonly property int idx: wsChip.activeId - wsChip.base - 1
                    x: idx * (wsChip.dot + 4) - 2
                    y: -2
                    width: wsChip.dot * (AetherBarConfig.wsNumbers ? 1 : 1.9) + 4
                    height: wsChip.dot + 4
                    radius: height / 2
                    color: AetherStyle.primary
                    Behavior on x { NumberAnimation { duration: 380; easing.type: Easing.OutBack; easing.overshoot: 1.3 } }
                }
                Row {
                    id: wsRow
                    spacing: 4
                    Repeater {
                        model: wsChip.count
                        Item {
                            id: cell
                            required property int index
                            readonly property int ws: wsChip.base + index + 1
                            readonly property bool on: wsChip.activeId === ws
                            readonly property bool used: !!wsChip.occ[ws]
                            width: on && !AetherBarConfig.wsNumbers ? wsChip.dot * 1.9 : wsChip.dot
                            height: wsChip.dot
                            Behavior on width { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                            Rectangle {
                                visible: !AetherBarConfig.wsNumbers
                                anchors.centerIn: parent
                                width: cell.on ? 0 : (cell.used ? 8 : 6)
                                height: width
                                radius: width / 2
                                color: cell.used ? AetherStyle.textMain : AetherStyle.outlineVar
                                Behavior on width { NumberAnimation { duration: 220 } }
                            }
                            Text {
                                visible: AetherBarConfig.wsNumbers
                                anchors.centerIn: parent
                                text: cell.ws
                                font.family: w.fontFamily
                                font.pixelSize: 12
                                font.weight: cell.on ? Font.Bold : Font.Medium
                                color: cell.on ? AetherStyle.palette.m3onPrimary : (cell.used ? AetherStyle.textMain : AetherStyle.textSub)
                            }
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -2
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (!cell.on) Hypr.focusWorkspace(cell.ws)
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- clock (Ryoku): click for the dashboard ----
    Component {
        id: clockC
        Chip {
            barH: w.barH
            active: AetherBarConfig.dashOpen && AetherBarConfig.dashTab === "home"
            onClicked: AetherBarConfig.toggleDash("home", w.screen)
            Label {
                text: Time.format(AetherBarConfig.clock24h ? "HH:mm" : "h:mm AP")
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Label {
                visible: AetherBarConfig.clockDate
                text: Time.format("ddd d MMM")
                color: AetherStyle.textSub
            }
        }
    }
    Component {
        id: dateC
        Chip {
            barH: w.barH
            onClicked: AetherBarConfig.toggleDash("home", w.screen)
            Sym { text: "calendar_today" }
            Label { text: Time.format("dddd d MMMM") }
        }
    }

    // ---- now playing (Ryoku RailMusic): cover, title, a little equaliser while playing ----
    Component {
        id: musicC
        Chip {
            id: mc
            readonly property var p: Players.active
            visible: !!p && (p.trackTitle || "") !== ""
            barH: w.barH
            pad: 6
            active: AetherBarConfig.dashOpen && (AetherBarConfig.dashTab === "music" || AetherBarConfig.dashTab === "lyrics")
            onClicked: m => {
                if (m.button === Qt.MiddleButton)
                    mc.p.togglePlaying();
                else
                    AetherBarConfig.toggleDash(m.button === Qt.RightButton ? "lyrics" : "music", w.screen);
            }
            onWheel: e => e.angleDelta.y < 0 ? mc.p.next() : mc.p.previous()
            ClippingRectangle {
                width: w.barH - 16
                height: width
                radius: width / 2
                color: AetherStyle.primaryContainer
                anchors.verticalCenter: parent.verticalCenter
                Image {
                    anchors.fill: parent
                    source: mc.p ? (mc.p.trackArtUrl || "") : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    RotationAnimator on rotation {
                        from: 0; to: 360; duration: 14000
                        loops: Animation.Infinite
                        running: !!mc.p && mc.p.isPlaying
                    }
                }
            }
            Label {
                text: mc.p ? mc.p.trackTitle + (mc.p.trackArtist ? "  ·  " + mc.p.trackArtist : "") : ""
                width: Math.min(implicitWidth, 220)
                elide: Text.ElideRight
            }
            Row {   // the equaliser
                spacing: 2
                height: 12
                anchors.verticalCenter: parent.verticalCenter
                Repeater {
                    model: 3
                    Rectangle {
                        required property int index
                        width: 3
                        radius: 1.5
                        color: AetherStyle.primary
                        anchors.bottom: parent.bottom
                        height: 4
                        SequentialAnimation on height {
                            running: !!mc.p && mc.p.isPlaying
                            loops: Animation.Infinite
                            NumberAnimation { to: 12; duration: 260 + index * 90; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 3; duration: 300 + index * 70; easing.type: Easing.InOutSine }
                        }
                    }
                }
            }
        }
    }
    Component {
        id: mediaC
        Chip {
            id: md
            readonly property var p: Players.active
            visible: !!p
            barH: w.barH
            hoverable: false
            Repeater {
                model: [["skip_previous", 0], [md.p && md.p.isPlaying ? "pause" : "play_arrow", 1], ["skip_next", 2]]
                Sym {
                    required property var modelData
                    text: modelData[0]
                    fill: 1
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: Qt.PointingHandCursor
                        onClicked: parent.modelData[1] === 0 ? md.p.previous() : parent.modelData[1] === 1 ? md.p.togglePlaying() : md.p.next()
                    }
                }
            }
        }
    }

    // ---- system monitor (Ryoku RailSysmon): CPU and memory, from Windows' own counters ----
    Component {
        id: sysC
        Chip {
            id: sc
            barH: w.barH
            property int cpu: 0
            property int ram: 0
            property var last: null
            onClicked: AetherBarConfig.toggleDash("system", w.screen)
            Timer {
                interval: 2000
                repeat: true
                running: true
                triggeredOnStart: true
                onTriggered: {
                    const s = Win.FsUtils.systemStats();
                    if (!s || !s.total)
                        return;
                    if (sc.last) {
                        const busy = (s.kernel - sc.last.kernel) + (s.user - sc.last.user);
                        const idle = s.idle - sc.last.idle;
                        if (busy > 0)
                            sc.cpu = Math.max(0, Math.min(100, Math.round((1 - idle / busy) * 100)));
                    }
                    sc.last = { idle: s.idle, kernel: s.kernel, user: s.user };
                    sc.ram = Math.round((1 - s.available / s.total) * 100);
                }
            }
            Sym { text: "memory"; color: sc.cpu > 80 ? AetherStyle.errorAccent : AetherStyle.textMain }
            Label { text: sc.cpu + "%" }
            Sym { text: "memory_alt"; color: sc.ram > 85 ? AetherStyle.errorAccent : AetherStyle.textMain }
            Label { text: sc.ram + "%" }
        }
    }

    // ---- tray ----
    // Right-click an icon for a small menu: its app menu, hide it (it moves behind the arrow) and the compact
    // tray (only the arrow shows; click it for every icon). Left-click opens the app.
    Component {
        id: trayC
        Chip {
            id: tc
            barH: w.barH
            hoverable: false
            pad: 6
            property bool expanded: false
            readonly property var all: SystemTray.items.values.filter(i => i.status !== Status.Passive)
            readonly property int hiddenCount: all.filter(i => (AetherBarConfig.trayHidden || []).indexOf(i.id) >= 0).length
            readonly property bool compact: AetherBarConfig.trayCompact
            visible: all.length > 0
            Repeater {
                id: trayRep
                model: tc.all.filter(i => tc.expanded || (!tc.compact && (AetherBarConfig.trayHidden || []).indexOf(i.id) < 0))
                MouseArea {
                    id: ti
                    required property var modelData
                    readonly property bool tucked: (AetherBarConfig.trayHidden || []).indexOf(modelData.id) >= 0
                    width: w.barH - 18
                    height: width
                    anchors.verticalCenter: parent.verticalCenter
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: m => {
                        if (m.button === Qt.LeftButton)
                            modelData.activate();
                        else if (m.button === Qt.MiddleButton)
                            modelData.secondaryActivate();
                        else {
                            trayMenu.item = modelData;
                            trayMenu.at = ti;
                            trayMenu.visible = true;
                        }
                    }
                    IconImage {
                        anchors.fill: parent
                        source: Icons.getTrayIcon(ti.modelData.id, ti.modelData.icon)
                        opacity: ti.tucked ? 0.55 : 1
                        scale: ti.containsMouse ? 1.15 : 1
                        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }
                    }
                }
            }
            // the arrow: shows the hidden icons (or, compact, all of them)
            MouseArea {
                id: arrow
                visible: tc.compact || tc.hiddenCount > 0
                width: w.barH - 18
                height: width
                anchors.verticalCenter: parent.verticalCenter
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: m => {
                    if (m.button === Qt.RightButton)
                        AetherBarConfig.trayCompact = !AetherBarConfig.trayCompact;
                    else
                        tc.expanded = !tc.expanded;
                }
                Sym {
                    anchors.centerIn: parent
                    text: tc.expanded ? "chevron_right" : "chevron_left"
                    opacity: arrow.containsMouse ? 1 : 0.75
                }
            }

            PopupWindow {
                id: trayMenu
                property var item: null
                property Item at: arrow
                anchor.item: at
                anchor.rect.y: at ? at.height + 10 : 0
                anchor.rect.x: at ? at.width / 2 - implicitWidth / 2 : 0
                implicitWidth: 230
                implicitHeight: tmCol.height + 12
                color: "transparent"
                visible: false
                Rectangle {
                    anchors.fill: parent
                    radius: 16
                    color: AetherStyle.surface
                    border.width: 1
                    border.color: Qt.alpha(AetherStyle.outlineVar, 0.6)
                    Column {
                        id: tmCol
                        x: 6
                        y: 6
                        width: parent.width - 12
                        Repeater {
                            model: trayMenu.item ? [
                                { icon: "menu", label: "App menu", run: () => trayMenu.item.secondaryActivate() },
                                { icon: AetherBarConfig.isTrayHidden(trayMenu.item.id) ? "visibility" : "visibility_off",
                                  label: AetherBarConfig.isTrayHidden(trayMenu.item.id) ? "Show on the bar" : "Hide behind the arrow",
                                  run: () => AetherBarConfig.toggleTrayHidden(trayMenu.item.id) },
                                { icon: AetherBarConfig.trayCompact ? "unfold_more" : "unfold_less",
                                  label: AetherBarConfig.trayCompact ? "Full tray" : "Compact tray",
                                  run: () => AetherBarConfig.trayCompact = !AetherBarConfig.trayCompact }
                            ] : []
                            Rectangle {
                                id: mr
                                required property var modelData
                                width: tmCol.width
                                height: 34
                                radius: 10
                                color: mra.containsMouse ? Qt.alpha(AetherStyle.primary, 0.14) : "transparent"
                                Sym {
                                    x: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: mr.modelData.icon
                                }
                                Label {
                                    x: 38
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: mr.modelData.label
                                }
                                MouseArea {
                                    id: mra
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        trayMenu.visible = false;
                                        mr.modelData.run();
                                    }
                                }
                            }
                        }
                    }
                    HoverHandler {
                        id: tmHover
                    }
                }
                Timer {    // closes once the mouse has left it for a moment
                    running: trayMenu.visible && !tmHover.hovered
                    interval: 1600
                    onTriggered: trayMenu.visible = false
                }
            }
        }
    }

    // ---- volume ----
    Component {
        id: volC
        Chip {
            barH: w.barH
            onClicked: m => {
                if (m.button === Qt.LeftButton && Audio.sink && Audio.sink.audio)
                    Audio.sink.audio.muted = !Audio.muted;
            }
            onWheel: e => e.angleDelta.y > 0 ? Audio.incrementVolume(0.05) : Audio.decrementVolume(0.05)
            Sym { text: Icons.getVolumeIcon(Audio.volume, Audio.muted) }
            Label { text: Math.round(Audio.volume * 100) + "%" }
        }
    }

    // ---- network ----
    Component {
        id: netC
        Chip {
            barH: w.barH
            pad: 8
            onClicked: Quickshell.execDetached(["explorer.exe", "ms-settings:network-status"])
            Sym {
                text: !Nmcli.isConnected ? "signal_wifi_off" : Win.Network.ethernetConnected ? "lan" : "wifi"
                color: Nmcli.isConnected ? AetherStyle.textMain : AetherStyle.textSub
            }
        }
    }

    // ---- battery (laptops only) ----
    Component {
        id: batC
        Chip {
            id: bc
            readonly property var dev: UPower.displayDevice
            readonly property bool laptop: !!dev && dev.ready && dev.isLaptopBattery
            readonly property int pct: laptop ? Math.round(dev.percentage * 100) : 0
            readonly property bool charging: laptop && dev.state === UPowerDeviceState.Charging
            visible: laptop
            barH: w.barH
            onClicked: Quickshell.execDetached(["explorer.exe", "ms-settings:batterysaver"])
            Sym {
                text: bc.charging ? "battery_charging_full" : bc.pct > 90 ? "battery_full" : bc.pct > 60 ? "battery_5_bar" : bc.pct > 30 ? "battery_3_bar" : bc.pct > 12 ? "battery_1_bar" : "battery_alert"
                color: bc.pct <= 12 && !bc.charging ? AetherStyle.errorAccent : AetherStyle.textMain
            }
            Label { text: bc.pct + "%" }
        }
    }

    // ---- notifications ----
    Component {
        id: notifC
        Chip {
            id: nc
            readonly property int n: Notifs.notClosed.length
            readonly property bool dnd: PersistentState.doNotDisturb
            barH: w.barH
            pad: 8
            active: !!w.st && w.st.sidebar
            onClicked: m => {
                if (m.button === Qt.RightButton)
                    PersistentState.doNotDisturb = !PersistentState.doNotDisturb;
                else if (w.st)
                    w.st.sidebar = !w.st.sidebar;
            }
            Sym {
                text: nc.dnd ? "notifications_off" : nc.n > 0 ? "notifications_unread" : "notifications"
                fill: nc.n > 0 ? 1 : 0
            }
            Rectangle {
                visible: nc.n > 0
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(18, cnt.implicitWidth + 8)
                height: 18
                radius: 9
                color: AetherStyle.primary
                Text {
                    id: cnt
                    anchors.centerIn: parent
                    text: nc.n > 99 ? "99+" : nc.n
                    font.family: w.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    color: AetherStyle.palette.m3onPrimary
                }
            }
        }
    }

    // ---- power: a row that opens out of the button (lock, sleep, sign out, restart, shut down) ----
    Component {
        id: powerC
        Chip {
            id: pc
            property bool open: false
            property int armed: -1
            barH: w.barH
            pad: 8
            active: open
            onClicked: { open = !open; armed = -1; }
            onHoveredChanged: if (!hovered) closeT.restart(); else closeT.stop()
            Timer { id: closeT; interval: 2200; onTriggered: { pc.open = false; pc.armed = -1; } }
            readonly property var acts: [
                { icon: "lock", tip: "Lock", run: () => WinSettings.ipc("lock", "lock", []) },
                { icon: "bedtime", tip: "Sleep", run: () => SessionManager.suspend() },
                { icon: "logout", tip: "Sign out", confirm: true, run: () => SessionManager.logout() },
                { icon: "restart_alt", tip: "Restart", confirm: true, run: () => SessionManager.reboot() },
                { icon: "power_settings_new", tip: "Shut down", confirm: true, run: () => SessionManager.poweroff() }
            ]
            Row {
                spacing: 2
                anchors.verticalCenter: parent.verticalCenter
                width: pc.open ? implicitWidth : 0
                clip: true
                Behavior on width { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                Repeater {
                    model: pc.acts.slice(0, 4)
                    Rectangle {
                        id: pb
                        required property var modelData
                        required property int index
                        width: w.barH - 12
                        height: width
                        radius: width / 2
                        color: pc.armed === index ? AetherStyle.errorContainer : (pa.containsMouse ? AetherStyle.palette.m3surfaceContainerHigh : "transparent")
                        MaterialIcon {
                            anchors.centerIn: parent
                            text: pc.armed === pb.index ? "check" : pb.modelData.icon
                            color: pc.armed === pb.index ? AetherStyle.textOnError : AetherStyle.textMain
                        }
                        MouseArea {
                            id: pa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (pb.modelData.confirm && pc.armed !== pb.index) {
                                    pc.armed = pb.index;
                                    return;
                                }
                                pc.open = false;
                                pb.modelData.run();
                            }
                        }
                    }
                }
            }
            Sym {
                text: pc.armed === 4 ? "check" : "power_settings_new"
                color: pc.open ? AetherStyle.errorAccent : AetherStyle.textMain
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    enabled: pc.open
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (pc.armed !== 4) {
                            pc.armed = 4;
                            return;
                        }
                        pc.open = false;
                        pc.acts[4].run();
                    }
                }
            }
        }
    }

    // ---- active window (end-4) ----
    Component {
        id: winC
        Chip {
            id: wc
            readonly property var t: Hypr.activeToplevel
            readonly property string title: t ? (t.title || "") : ""
            visible: title !== ""
            barH: w.barH
            hoverable: false
            IconImage {
                width: 16
                height: 16
                anchors.verticalCenter: parent.verticalCenter
                visible: source !== ""
                source: {
                    const c = wc.t && wc.t.lastIpcObject ? (wc.t.lastIpcObject.class || "") : "";
                    const e = c !== "" ? DesktopEntries.heuristicLookup(c) : null;
                    return e && e.icon ? Quickshell.iconPath(e.icon, true) : "";
                }
            }
            Label {
                text: wc.title
                width: Math.min(implicitWidth, 280)
                elide: Text.ElideRight
                color: AetherStyle.textSub
            }
        }
    }

    // ---- weather (end-4) ----
    Component {
        id: weatherC
        Chip {
            barH: w.barH
            visible: !!Weather.cc
            onClicked: AetherBarConfig.toggleDash("home", w.screen)
            Sym { text: Weather.icon }
            Label { text: Weather.temp }
        }
    }

    // ---- keyboard layout (DarkShadowShell) ----
    Component {
        id: kbC
        Chip {
            barH: w.barH
            hoverable: false
            Sym { text: Hypr.capsLock ? "keyboard_capslock_badge" : "keyboard" }
            Label { text: Hypr.kbLayout }
        }
    }

    Component {
        id: spacerC
        Item {
            width: 9
            height: w.barH - 8
            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: parent.height * 0.5
                color: AetherStyle.outlineVar
            }
        }
    }
}
