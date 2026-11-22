import QtQuick
import QtQuick.Effects
import Quickshell
import Quickshell.Wayland
import Quickshell.Widgets
import qs.services as Cae
import qs.modules.aethersettings as AS
import "../catalog.js" as Catalog

// GNOME Activities: search entry on top, the current workspace's windows as live previews, the dash along the
// bottom (favourites + running apps + Show Apps). Typing searches apps; Show Apps opens the app grid.
// Super, Esc or a click on empty space closes it.
PanelWindow {
    id: win

    required property var distro
    property bool startApps
    property bool apps: startApps
    property string query: ""
    signal dismissed

    screen: Quickshell.screens.find(s => s.name === Cae.Hypr.focusedMonitor?.name) ?? Quickshell.screens[0]
    anchors {
        top: true
        bottom: true
        left: true
        right: true
    }
    margins.top: Adw.barHeight
    exclusionMode: ExclusionMode.Ignore
    color: "transparent"
    WlrLayershell.layer: WlrLayer.Top
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
    WlrLayershell.namespace: "aether-gnome-overview"

    // ---- data
    // the distro's own apps first-class in the grid and search, like on Debian
    readonly property var builtins: [
        { name: "Settings", genericName: "Settings", keywords: ["preferences", "control panel", "wifi", "sound", "display", "background"], icon: "preferences-system", execute: () => Cae.WinSettings.distroCommand("settings:wifi") },
        { name: "System Monitor", genericName: "Task Manager", keywords: ["task manager", "processes", "cpu", "memory"], icon: "utilities-system-monitor", execute: () => Cae.WinSettings.distroCommand("monitor") },
        // the way back out of the distro look: Aether's own settings (other shells, Mix & match, distro looks)
        { name: "Aether Settings", genericName: "Shells and looks", keywords: ["aether", "shell", "shells", "switch shell", "mix", "match", "caelestia", "noctalia", "distro", "look", "global settings"], icon: "file:///" + Quickshell.shellPath("assets/logo.svg"), execute: () => AS.AetherSettings.open("") }
    ]
    // Settings panels as search results (Search > Settings), like GNOME's overview
    readonly property var settingsPanels: [["wifi", "Wi-Fi", "wireless network internet"], ["network", "Network", "ethernet wired vpn proxy"], ["bluetooth", "Bluetooth", "devices headphones"], ["displays", "Displays", "monitor screen brightness resolution"], ["sound", "Sound", "volume audio speaker microphone"], ["power", "Power", "battery sleep game mode"], ["multitasking", "Multitasking", "workspaces tiling"], ["appearance", "Appearance", "background wallpaper accent colour dark"], ["apps", "Apps", "applications programs"], ["notifications", "Notifications", "do not disturb"], ["mouse", "Mouse & Touchpad", "pointer speed scroll click"], ["keyboard", "Keyboard", "shortcuts repeat layout"], ["printers", "Printers", "print"], ["accessibility", "Accessibility", "animation"], ["privacy", "Privacy & Security", "lock screen"], ["datetime", "Date & Time", "clock time zone"], ["about", "About", "system info memory processor"]]
        .map(p => ({ name: p[1], genericName: "Settings", keywords: p[2].split(" "), icon: "preferences-system", execute: () => Cae.WinSettings.distroCommand("settings:" + p[0]) }))
    readonly property var allApps: builtins.concat([...DesktopEntries.applications.values].filter(e => !e.noDisplay)).sort((a, b) => a.name.localeCompare(b.name))
    // GNOME's default app folders (org.gnome.desktop.app-folders): "System" and "Utilities", after the apps.
    // Windows' admin tools and accessories go in them, matched by name.
    readonly property var folderDefs: [
        { name: "System", match: /^(command prompt|windows powershell|powershell|terminal|windows terminal|task manager|system monitor|control panel|registry editor|event viewer|services|computer management|disk cleanup|defragment.*|system information|resource monitor|performance monitor|windows tools|administrative tools|system configuration|device manager|disk management|odbc.*|task scheduler|recovery drive|component services|print management|windows memory diagnostic|local security policy|windows defender firewall.*|iscsi initiator)$/ },
        { name: "Utilities", match: /^(calculator|notepad|text editor|character map|snipping tool|paint|wordpad|remote desktop connection|magnifier|on-screen keyboard|narrator|steps recorder|math input panel|quick assist|windows media player.*|xps viewer|windows fax and scan|sound recorder|clock|sticky notes)$/ }
    ]
    property var openFolder: null
    readonly property var gridApps: {
        const folders = folderDefs.map(f => ({ name: f.name, folder: true, apps: allApps.filter(e => f.match.test(e.name.toLowerCase())) })).filter(f => f.apps.length);
        const inFolder = e => folders.some(f => f.apps.includes(e));
        return allApps.filter(e => !inFolder(e)).concat(folders);
    }
    readonly property var results: {
        const q = query.trim().toLowerCase();
        if (!q)
            return [];
        const score = e => {
            const n = e.name.toLowerCase();
            const s = n.startsWith(q) ? 0 : n.includes(q) ? 1 : (e.genericName ?? "").toLowerCase().includes(q) || (e.keywords ?? []).some(k => k.toLowerCase().includes(q)) ? 2 : 9;
            // Aether's own entries (the way back to the other shells) win ties, so "aether" + Enter opens Aether Settings
            return s < 9 && win.builtins.includes(e) ? s - 0.5 : s;
        };
        const panels = Cae.WinSettings.distroSearchSettings ? settingsPanels : [];
        return allApps.concat(panels).map(e => ({ e, s: score(e) })).filter(x => x.s < 9).sort((a, b) => a.s - b.s).slice(0, 18).map(x => x.e);
    }
    readonly property var windows: {
        const own = ["qs", "qsw", "quickshell"];
        const all = ToplevelManager.toplevels.values.filter(t => t && t.appId && !own.includes(t.appId.toLowerCase()) && !t.parent);
        const here = (Cae.Hypr.focusedWorkspace?.toplevels?.values ?? []).map(h => h.wayland).filter(t => !!t);
        return here.length ? all.filter(t => here.includes(t)) : all;
    }
    readonly property var dashApps: {
        const out = [];
        const add = (id, entry, toplevel) => {
            const key = (id || "").toLowerCase();
            if (!key || out.some(o => o.key === key))
                return;
            // a window whose id is a hash (Zen, Store apps) joins its app's pinned icon instead of a second one
            const same = entry ? out.find(o => o.entry === entry) : null;
            if (same) {
                if (!same.toplevel)
                    same.toplevel = toplevel;
                return;
            }
            out.push({ key, entry, toplevel });
        };
        const findWin = k => ToplevelManager.toplevels.values.find(t => (t.appId ?? "").toLowerCase().replace(/[.]exe$/, "") === k);
        for (const fav of Catalog.favKeys(distro, k => !!DesktopEntries.heuristicLookup(k) || !!findWin(k)))
            add(fav, DesktopEntries.heuristicLookup(fav), findWin(fav));
        for (const t of ToplevelManager.toplevels.values.filter(t => t && t.appId && !["qs", "qsw", "quickshell"].includes(t.appId.toLowerCase()) && !t.parent))
            add(t.appId, DesktopEntries.heuristicLookup(t.appId), t);
        return out.filter(o => o.entry || o.toplevel);
    }

    function close(): void {
        dismissed();
    }
    function launch(entry: var): void {
        entry?.execute();
        close();
    }
    function iconFor(entry: var, appId: string): string {
        if (String(entry?.icon ?? "").startsWith("file:"))
            return entry.icon;
        return Quickshell.iconPath(entry?.icon ?? appId, true) || Quickshell.shellPath("assets/app-generic.svg");
    }

    // ---- background: the wallpaper, dimmed and blurred like GNOME's overview
    Item {
        anchors.fill: parent
        Image {
            id: wall
            anchors.fill: parent
            source: win.distro?.wallpaper ? Quickshell.shellPath(`distros/${win.distro.wallpaper}`) : ""
            sourceSize: Qt.size(win.width / 4, win.height / 4)
            fillMode: Image.PreserveAspectCrop
            visible: false
        }
        MultiEffect {
            anchors.fill: parent
            source: wall
            blurEnabled: true
            blur: 1
            blurMax: 48
            brightness: -0.35
        }
        opacity: 0
        Component.onCompleted: opacity = 1
        Behavior on opacity { NumberAnimation { duration: 180 } }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: win.close()
    }

    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Escape) {
                if (win.openFolder) win.openFolder = null; else if (win.query) win.query = ""; else if (win.apps && !win.startApps) win.apps = false; else win.close();
                event.accepted = true;
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (win.results.length) win.launch(win.results[0]);
                event.accepted = true;
            } else if (event.key === Qt.Key_Backspace) {
                win.query = win.query.slice(0, -1);
                event.accepted = true;
            } else if (event.key === Qt.Key_Super_L || event.key === Qt.Key_Super_R || event.key === Qt.Key_Meta) {
                win.close();
                event.accepted = true;
            } else if (event.text && event.text.length === 1 && event.text.charCodeAt(0) >= 32) {
                win.query += event.text;
                event.accepted = true;
            }
        }
    }

    // ---- search entry
    Rectangle {
        id: search
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 22
        width: 320
        height: 38
        radius: 19
        color: Qt.rgba(1, 1, 1, 0.1)
        border.color: win.query ? Adw.accent : "transparent"
        border.width: 2
        Text {
            id: glass
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: "search"
            font.family: Adw.icons
            font.pixelSize: 18
            color: Adw.dim
        }
        Text {
            anchors.left: glass.right
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: win.query || "Type to search"
            color: win.query ? Adw.text : Adw.dim
            font.family: Adw.font
            font.pixelSize: 14
            elide: Text.ElideLeft
        }
    }

    // ---- window previews of the current workspace
    Flow {
        id: previews
        visible: !win.query && !win.apps
        anchors.top: search.bottom
        anchors.bottom: dash.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 48
        spacing: 32
        layoutDirection: Qt.LeftToRight

        readonly property int n: win.windows.length
        readonly property int cols: Math.max(1, Math.ceil(Math.sqrt(n * 1.6)))
        readonly property int rows: Math.max(1, Math.ceil(n / cols))
        readonly property real cellW: Math.min(560, (width - (cols - 1) * spacing) / cols)
        readonly property real cellH: Math.min(cellW * 0.62 + 36, (height - (rows - 1) * spacing) / rows)
        leftPadding: Math.max(0, (width - Math.min(n, cols) * cellW - (Math.min(n, cols) - 1) * spacing) / 2)
        topPadding: Math.max(0, (height - rows * cellH - (rows - 1) * spacing) / 2)

        Repeater {
            model: win.windows
            Item {
                id: card
                required property var modelData
                width: previews.cellW
                height: previews.cellH
                scale: cardArea.containsMouse ? 1.03 : 1
                Behavior on scale { NumberAnimation { duration: 120 } }
                Rectangle {
                    id: frame
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width
                    height: parent.height - 36
                    radius: 12
                    color: "transparent"
                    border.width: cardArea.containsMouse ? 4 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.5)
                    WindowThumbnail {
                        anchors.fill: parent
                        anchors.margins: 4
                        captureSource: card.modelData
                    }
                }
                IconImage {
                    anchors.horizontalCenter: frame.horizontalCenter
                    anchors.verticalCenter: frame.bottom
                    implicitSize: 44
                    source: win.iconFor(DesktopEntries.heuristicLookup(card.modelData.appId), card.modelData.appId)
                }
                Rectangle {
                    visible: cardArea.containsMouse
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: Math.min(parent.width, titleText.implicitWidth + 20)
                    height: 26
                    radius: 13
                    color: Adw.popover
                    Text {
                        id: titleText
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, card.width - 20)
                        text: card.modelData.title ?? ""
                        color: Adw.text
                        font.family: Adw.font
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }
                MouseArea {
                    id: cardArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { card.modelData.activate(); win.close(); }
                }
                Rectangle {   // close button, top-right on hover
                    visible: cardArea.containsMouse || closeArea.containsMouse
                    anchors.right: frame.right
                    anchors.top: frame.top
                    anchors.margins: -10
                    width: 28
                    height: 28
                    radius: 14
                    color: Adw.card
                    Text {
                        anchors.centerIn: parent
                        text: "close"
                        font.family: Adw.icons
                        font.pixelSize: 16
                        color: Adw.text
                    }
                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: card.modelData.close()
                    }
                }
            }
        }
    }

    // ---- app grid (Show Apps) and search results share one grid
    GridView {
        id: grid
        visible: win.apps || !!win.query
        anchors.top: search.bottom
        anchors.topMargin: 30
        anchors.bottom: dash.top
        anchors.bottomMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 120, 8 * cellWidth)
        cellWidth: 150
        cellHeight: 150
        clip: true
        model: win.query ? win.results : win.gridApps
        delegate: Item {
            id: cell
            required property var modelData
            required property int index
            width: grid.cellWidth
            height: grid.cellHeight
            Rectangle {
                anchors.fill: parent
                anchors.margins: 6
                radius: 18
                color: appArea.containsMouse || (win.query && cell.index === 0) ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
            }
            IconImage {
                visible: !cell.modelData.folder
                anchors.horizontalCenter: parent.horizontalCenter
                y: 22
                implicitSize: 72
                source: cell.modelData.folder ? "" : win.iconFor(cell.modelData, "")
            }
            Rectangle {   // a folder: its first four apps' icons in a 2x2 tile, like GNOME
                visible: !!cell.modelData.folder
                anchors.horizontalCenter: parent.horizontalCenter
                y: 22
                width: 72
                height: 72
                radius: 18
                color: Qt.rgba(1, 1, 1, 0.14)
                Grid {
                    anchors.centerIn: parent
                    columns: 2
                    spacing: 6
                    Repeater {
                        model: cell.modelData.folder ? cell.modelData.apps.slice(0, 4) : []
                        IconImage {
                            required property var modelData
                            implicitSize: 26
                            source: win.iconFor(modelData, "")
                        }
                    }
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 104
                width: parent.width - 16
                horizontalAlignment: Text.AlignHCenter
                text: modelData.name
                color: Adw.text
                font.family: Adw.font
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            MouseArea {
                id: appArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: cell.modelData.folder ? win.openFolder = cell.modelData : win.launch(cell.modelData)
            }
        }
    }

    // ---- an open app folder: a dimmed backdrop and a rounded card with the folder's apps
    Item {
        anchors.fill: parent
        visible: !!win.openFolder
        z: 5
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.6)
        }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: win.openFolder = null
        }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 120, folderGrid.cellWidth * Math.min(6, Math.max(3, folderGrid.count)) + 40)
            height: Math.min(parent.height - 160, folderTitle.height + 40 + folderGrid.cellHeight * Math.ceil(folderGrid.count / Math.max(1, Math.floor((width - 40) / folderGrid.cellWidth))))
            radius: 28
            color: Qt.rgba(0.16, 0.16, 0.17, 0.98)
            MouseArea { anchors.fill: parent }
            Text {
                id: folderTitle
                anchors.horizontalCenter: parent.horizontalCenter
                y: 18
                text: win.openFolder?.name ?? ""
                color: Adw.text
                font.family: Adw.font
                font.pixelSize: 20
                font.weight: Font.Bold
            }
            GridView {
                id: folderGrid
                anchors.top: folderTitle.bottom
                anchors.topMargin: 12
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                cellWidth: 140
                cellHeight: 140
                clip: true
                model: win.openFolder?.apps ?? []
                delegate: Item {
                    id: fcell
                    required property var modelData
                    width: folderGrid.cellWidth
                    height: folderGrid.cellHeight
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 6
                        radius: 18
                        color: fArea.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                    }
                    IconImage {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 18
                        implicitSize: 64
                        source: win.iconFor(fcell.modelData, "")
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 92
                        width: parent.width - 16
                        horizontalAlignment: Text.AlignHCenter
                        text: fcell.modelData.name
                        color: Adw.text
                        font.family: Adw.font
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: fArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: win.launch(fcell.modelData)
                    }
                }
            }
        }
    }

    // ---- the dash
    Rectangle {
        id: dash
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: dashRow.implicitWidth + 16
        height: 72
        radius: 22
        color: Qt.rgba(1, 1, 1, 0.12)
        MouseArea { anchors.fill: parent }

        Row {
            id: dashRow
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: win.dashApps
                Item {
                    id: dashItem
                    required property var modelData
                    width: 60
                    height: 64
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: 14
                        color: dashArea.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : "transparent"
                    }
                    IconImage {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -3
                        implicitSize: 44
                        source: win.iconFor(dashItem.modelData.entry, dashItem.modelData.key)
                    }
                    Rectangle {   // running dot
                        visible: !!dashItem.modelData.toplevel
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 3
                        width: 5
                        height: 5
                        radius: 2.5
                        color: Adw.text
                    }
                    MouseArea {
                        id: dashArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (dashItem.modelData.toplevel) {
                                dashItem.modelData.toplevel.activate();
                                win.close();
                            } else {
                                win.launch(dashItem.modelData.entry);
                            }
                        }
                    }
                }
            }
            Rectangle {
                width: 1
                height: 44
                anchors.verticalCenter: parent.verticalCenter
                color: Qt.rgba(1, 1, 1, 0.2)
                visible: win.dashApps.length > 0
            }
            Item {
                width: 60
                height: 64
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 14
                    color: showArea.containsMouse || win.apps ? Qt.rgba(1, 1, 1, 0.15) : "transparent"
                }
                Text {
                    anchors.centerIn: parent
                    text: "apps"
                    font.family: Adw.icons
                    font.pixelSize: 36
                    color: Adw.text
                }
                MouseArea {
                    id: showArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { win.query = ""; win.apps = !win.apps; }
                }
            }
        }
    }
}
