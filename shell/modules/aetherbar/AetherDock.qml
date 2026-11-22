// Aether shell (AGPL-3.0-or-later). The island dock: hidden on the left edge of every screen, it slides out as an
// island of your apps when the mouse touches the edge. Pinned apps first, then the ones running (a dot shows
// which are open, a longer one the focused app). Icons swell under the mouse. Left-click opens or focuses,
// middle-click opens another, right-click pins or unpins. The app model follows DarkShadowShell's dock (MIT).
// Shown while Mix & match > Dock is "aether" (and the dock is on in Settings > Bar > Aether bar).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import Quickshell
import Quickshell.Wayland
import Quickshell.Widgets
import qs.services
import qs.utils

Variants {
    model: Screens.screens

    Scope {
        id: dock

        required property ShellScreen modelData
        readonly property var cfg: AetherBarConfig
        readonly property real icon: cfg.dockIcon
        readonly property real dockW: icon + 22

        property bool reveal: false
        property real t: 0
        Behavior on t { NumberAnimation { duration: dock.reveal ? 520 : 260; easing.type: Easing.Linear } }
        onRevealChanged: t = reveal ? 1 : 0
        // out with a little overshoot (an island popping out), back in smoothly
        readonly property real et: reveal ? 1 + 2.4 * Math.pow(t - 1, 3) + 1.4 * Math.pow(t - 1, 2) : 1 - Math.pow(1 - t, 3)
        function stg(i) {
            const k = Math.max(0, Math.min(1, (t - Math.min(i, 14) * 0.035) / 0.6));
            return 1 - Math.pow(1 - k, 3);
        }

        Timer {
            id: hideT
            interval: 420
            onTriggered: dock.reveal = false
        }

        function build() {
            const tl = cfg.dockRunning ? ToplevelManager.toplevels.values : [];
            const out = [];
            const used = {};
            const apps = cfg.dockApps || [];
            for (let i = 0; i < apps.length; i++) {
                const e = DesktopEntries.heuristicLookup(apps[i]);
                if (!e)
                    continue;
                const ids = [apps[i].toLowerCase(), (e.id || "").toLowerCase(), (e.startupClass || "").toLowerCase()];
                let t = null;
                for (let j = 0; j < tl.length; j++) {
                    const id = (tl[j].appId || "").toLowerCase();
                    if (id !== "" && ids.indexOf(id) >= 0) {
                        t = tl[j];
                        break;
                    }
                }
                if (t)
                    used[(t.appId || "").toLowerCase()] = true;
                out.push({ entry: e, tl: t, key: apps[i], pinned: true });
            }
            for (let j = 0; j < tl.length; j++) {
                const id = (tl[j].appId || "").toLowerCase();
                if (id === "" || used[id])
                    continue;
                used[id] = true;
                out.push({ entry: DesktopEntries.heuristicLookup(tl[j].appId), tl: tl[j], key: tl[j].appId, pinned: false });
            }
            return out;
        }
        readonly property var items: build()
        function iconFor(it) {
            const names = [it.entry ? it.entry.icon : "", it.tl ? (it.tl.appId || "") : "", it.tl ? (it.tl.appId || "").toLowerCase() : ""];
            for (let i = 0; i < names.length; i++) {
                const p = names[i] ? Quickshell.iconPath(names[i], true) : "";
                if (p !== "")
                    return p;
            }
            return Quickshell.iconPath("application-x-executable", true);
        }
        function nameFor(it) {
            return it.entry ? it.entry.name : (it.tl ? (it.tl.title || it.tl.appId) : "");
        }

        // ---- the edge that brings it out ----
        PanelWindow {
            screen: dock.modelData
            visible: dock.cfg.dock && !dock.reveal && dock.t < 0.05
            color: "transparent"
            anchors.left: true
            implicitWidth: 3
            implicitHeight: Math.round((dock.modelData.height || 900) * 0.7)
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.layer: WlrLayer.Top
            WlrLayershell.namespace: "aether-dock-edge"
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onEntered: {
                    hideT.stop();
                    dock.reveal = true;
                }
            }
        }

        // ---- the island ----
        PanelWindow {
            id: win
            screen: dock.modelData
            visible: dock.cfg.dock && (dock.reveal || dock.t > 0.01)
            color: "transparent"
            anchors.left: true
            implicitWidth: dock.icon * 1.6 + 40
            implicitHeight: Math.min((dock.modelData.height || 900) - 80, island.height + 60)
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.namespace: "aether-dock"

            property real mouseY: -1000

            MouseArea {
                id: hover
                anchors.fill: parent
                hoverEnabled: true
                onEntered: hideT.stop()
                onExited: {
                    win.mouseY = -1000;
                    hideT.restart();
                }
                onPositionChanged: m => win.mouseY = m.y
            }

            Rectangle {
                id: island
                x: 10 - (1 - dock.et) * (dock.dockW + 30)
                y: (parent.height - height) / 2
                width: dock.dockW
                height: col.height + 22 + (nameTag.visible ? 0 : 0)
                radius: Math.min(28, dock.dockW / 2)
                color: Qt.alpha(AetherStyle.surface, Math.max(0.6, AetherBarConfig.barOpacity))
                border.width: 1
                border.color: Qt.alpha(AetherStyle.outlineVar, 0.6)
                opacity: Math.min(1, dock.t * 2)
                scale: 0.86 + 0.14 * dock.et
                transformOrigin: Item.Left
                Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: "#000000"
                    shadowOpacity: 0.4
                    shadowBlur: 0.8
                    shadowHorizontalOffset: 3
                }

                Column {
                    id: col
                    x: 0
                    y: 11
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: dock.items
                        Item {
                            id: cell
                            required property var modelData
                            required property int index
                            readonly property real cy: island.y + col.y + y + height / 2
                            readonly property real d: Math.abs(win.mouseY - cy)
                            readonly property real mag: AetherBarConfig.dockMagnify ? 1 + 0.42 * Math.max(0, 1 - d / (dock.icon * 2.2)) : 1
                            readonly property bool running: !!modelData.tl
                            readonly property bool focused: running && modelData.tl.activated
                            width: col.width
                            height: dock.icon * (0.85 + 0.15 * mag)
                            opacity: dock.stg(index)
                            transform: Translate { x: (1 - dock.stg(cell.index)) * -24 }

                            // open / focused marker on the outer edge
                            Rectangle {
                                x: 3
                                anchors.verticalCenter: parent.verticalCenter
                                width: 4
                                height: cell.focused ? dock.icon * 0.55 : 6
                                radius: 2
                                visible: cell.running
                                color: cell.focused ? AetherStyle.primary : AetherStyle.textSub
                                Behavior on height { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }
                            }
                            IconImage {
                                id: ic
                                anchors.centerIn: parent
                                implicitSize: dock.icon
                                source: dock.iconFor(cell.modelData)
                                scale: (ca.pressed ? 0.88 : 1) * cell.mag
                                transformOrigin: Item.Left
                                anchors.horizontalCenterOffset: -dock.icon * (cell.mag - 1) * 0.3
                                Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                            }
                            // a small bounce when an app is launched
                            SequentialAnimation {
                                id: bounce
                                NumberAnimation { target: ic; property: "anchors.horizontalCenterOffset"; to: 14; duration: 160; easing.type: Easing.OutQuad }
                                NumberAnimation { target: ic; property: "anchors.horizontalCenterOffset"; to: 0; duration: 420; easing.type: Easing.OutBounce }
                            }
                            MouseArea {
                                id: ca
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                                onEntered: {
                                    nameTag.text = dock.nameFor(cell.modelData);
                                    nameTag.cy = cell.cy;
                                }
                                onPositionChanged: m => win.mouseY = cell.mapToItem(hover, 0, m.y).y
                                onClicked: m => {
                                    const it = cell.modelData;
                                    if (m.button === Qt.RightButton) {
                                        const id = it.entry ? (it.entry.id || it.key) : it.key;
                                        AetherBarConfig.togglePin(String(id).replace(/\.desktop$/, ""));
                                    } else if (m.button === Qt.LeftButton && it.tl) {
                                        it.tl.activate();
                                    } else if (it.entry) {
                                        bounce.restart();
                                        it.entry.execute();
                                    }
                                }
                            }
                        }
                    }

                    // the apps button: the launcher
                    Item {
                        width: col.width
                        height: dock.icon * 0.8
                        visible: true
                        Rectangle {
                            anchors.centerIn: parent
                            width: dock.icon * 0.72
                            height: width
                            radius: width / 3
                            color: la.containsMouse ? AetherStyle.primaryContainer : Qt.alpha(AetherStyle.textMain, 0.08)
                            Behavior on color { ColorAnimation { duration: 150 } }
                            Text {
                                anchors.centerIn: parent
                                text: "apps"
                                font.family: "Material Symbols Rounded"
                                font.pixelSize: dock.icon * 0.4
                                color: la.containsMouse ? AetherStyle.textOnPrimaryContainer : AetherStyle.textMain
                            }
                        }
                        MouseArea {
                            id: la
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: {
                                nameTag.text = dock.items.length === 0 ? "Apps · right-click one in the dock to pin it" : "All apps";
                                nameTag.cy = island.y + col.y + parent.y + height / 2;
                            }
                            onClicked: {
                                dock.reveal = false;
                                ShellState.forScreen(dock.modelData).launcher = true;
                            }
                        }
                    }
                }
            }
        }

        // the hovered app's name, beside the island (its own small window so the island's stays narrow)
        PanelWindow {
            screen: dock.modelData
            visible: win.visible && hover.containsMouse && nameTag.text !== "" && win.mouseY > 0
            color: "transparent"
            anchors.left: true
            anchors.top: true
            margins.left: dock.dockW + 18
            margins.top: Math.max(0, ((dock.modelData.height || 900) - win.implicitHeight) / 2 + nameTag.cy - 16)
            implicitWidth: nameTag.implicitWidth + 24
            implicitHeight: 32
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.namespace: "aether-dock-tip"
            Rectangle {
                anchors.fill: parent
                radius: 16
                color: AetherStyle.palette.m3inverseSurface
                Text {
                    id: nameTag
                    property real cy: 0
                    anchors.centerIn: parent
                    color: AetherStyle.palette.m3inverseOnSurface
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
            }
        }
    }
}
