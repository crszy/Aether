import QtQuick
import Quickshell
import Quickshell.Wayland
import Quickshell.Widgets
import qs.services as Cae
import qs.components.effects
import "../catalog.js" as Catalog

// The taskbar: GNOME's dash kept on screen at the bottom (the Dash to Dock extension most Debian GNOME users
// add). Favourites, running apps (a dot each, two for several windows), Show Apps at the end. Click a running
// app to bring it up (again to cycle its windows), click a favourite to start it.
PanelWindow {
    id: win

    required property var distro
    signal appsRequested

    anchors.bottom: true
    margins.bottom: 6
    implicitWidth: dock.width
    implicitHeight: 70
    exclusionMode: ExclusionMode.Normal
    exclusiveZone: 76
    color: "transparent"
    WlrLayershell.layer: WlrLayer.Top
    WlrLayershell.namespace: "aether-gnome-dock"

    readonly property var own: ["qs", "qsw", "quickshell"]
    readonly property var running: ToplevelManager.toplevels.values.filter(t => t && t.appId && !own.includes(t.appId.toLowerCase()) && !t.parent)
    readonly property var apps: {
        const out = [];
        const add = key => {
            key = (key || "").toLowerCase().replace(/[.]exe$/, "");
            if (!key || out.some(o => o.key === key))
                return;
            const wins = running.filter(t => t.appId.toLowerCase().replace(/[.]exe$/, "") === key);
            const entry = DesktopEntries.heuristicLookup(key);
            // one icon per app: a window whose id is a hash (Zen, Store apps) joins its app's pinned icon
            const same = entry ? out.find(o => o.entry === entry) : null;
            if (same) {
                for (const w of wins)
                    if (!same.wins.includes(w))
                        same.wins.push(w);
                return;
            }
            if (entry || wins.length)
                out.push({ key, entry, wins });
        };
        const isRunning = k => running.some(t => t.appId.toLowerCase().replace(/[.]exe$/, "") === k);
        for (const fav of Catalog.favKeys(distro, k => !!DesktopEntries.heuristicLookup(k) || isRunning(k)))
            add(fav);
        const favs = out.length;
        for (const t of running)
            add(t.appId);
        out.forEach((o, i) => o.sepBefore = favs > 0 && i === favs);
        return out;
    }
    property var cycle: ({})

    Rectangle {
        id: dock
        anchors.horizontalCenter: parent.horizontalCenter
        width: row.implicitWidth + 12
        height: parent.height
        radius: 20
        color: Adw.glass ? "transparent" : Qt.rgba(0.12, 0.12, 0.13, 0.9)
        border.color: Adw.glass ? "transparent" : Adw.border

        Loader {
            anchors.fill: parent
            active: Adw.glass
            sourceComponent: LiquidGlass {
                radius: dock.radius
                source: Adw.wallpaper
                screenSize: Qt.size(win.screen?.width ?? 1920, win.screen?.height ?? 1080)
                // the dock window sits centred at the bottom, 6 px up
                windowOrigin: Qt.point(((win.screen?.width ?? 1920) - win.width) / 2, (win.screen?.height ?? 1080) - 6 - win.height)
            }
        }

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 2
            Repeater {
                model: win.apps
                Item {
                    id: app
                    required property var modelData
                    readonly property bool focused: modelData.wins.some(w => w.activated)
                    width: 58
                    height: 62
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: 14
                        color: area.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : app.focused ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    }
                    IconImage {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -3
                        implicitSize: 42
                        source: Quickshell.iconPath(app.modelData.entry?.icon ?? app.modelData.key, true) || Quickshell.shellPath("assets/app-generic.svg")
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 3
                        spacing: 3
                        Repeater {
                            model: Math.min(app.modelData.wins.length, 2)
                            Rectangle { width: app.focused ? 10 : 5; height: 5; radius: 2.5; color: app.focused ? Adw.accent : Adw.text }
                        }
                    }
                    Rectangle {   // GNOME's dash: a thin line between the favourites and the other running apps
                        visible: !!app.modelData.sepBefore
                        x: -1.5
                        width: 1
                        height: 40
                        anchors.verticalCenter: parent.verticalCenter
                        color: Qt.rgba(1, 1, 1, 0.2)
                    }
                    Rectangle {   // tooltip
                        visible: area.containsMouse
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.top
                        anchors.bottomMargin: 8
                        width: tip.implicitWidth + 20
                        height: 28
                        radius: 14
                        color: Adw.popover
                        Text { id: tip; anchors.centerIn: parent; text: app.modelData.entry?.name ?? app.modelData.key; color: Adw.text; font.family: Adw.font; font.pixelSize: 13 }
                    }
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                        onClicked: mouse => {
                            const wins = app.modelData.wins;
                            if (!wins.length || mouse.button === Qt.MiddleButton) {
                                app.modelData.entry?.execute();
                                return;
                            }
                            const i = ((win.cycle[app.modelData.key] ?? -1) + (app.focused ? 1 : 0)) % wins.length;
                            win.cycle[app.modelData.key] = i;
                            wins[Math.max(0, i)].activate();
                        }
                    }
                }
            }
            Rectangle {
                width: 1
                height: 40
                anchors.verticalCenter: parent.verticalCenter
                color: Qt.rgba(1, 1, 1, 0.2)
            }
            Item {
                width: 58
                height: 62
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 14
                    color: showArea.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : "transparent"
                }
                Text {
                    anchors.centerIn: parent
                    text: "apps"
                    font.family: Adw.icons
                    font.pixelSize: 34
                    color: Adw.text
                }
                MouseArea {
                    id: showArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: win.appsRequested()
                }
            }
        }
    }
}
