pragma ComponentBehavior: Bound

// Bar entry "apps": your open windows as app icons, like a taskbar.
//   click: focus it (click the focused one to minimise)   middle-click: close   scroll: cycle through them
// Each app shows where it lives: a badge with its workspace (and its monitor when it is on another one), and the
// list can be limited to this bar's monitor or to what is on screen there, and grouped by workspace or monitor.
//
// Everything about it is set in ~/.config/caelestia/taskbar.json (written with all options the first time, reloaded
// live on save) - see `defaults` below for every option and what it does.
import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Hyprland
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Widgets
import Caelestia.Config
import qs.components
import qs.services
import qs.utils

StyledRect {
    id: root

    // ------------------------------------------------------------ settings (taskbar.json)
    readonly property var defaults: ({
        // which apps this bar lists: "all" | "monitor" (apps on this bar's monitor) | "workspace" (only the workspace - or
        // the canvas - this monitor shows; switching pops the new ones in)
        show: "workspace",
        // "none" | "workspace" (a gap and label between workspaces) | "monitor"
        group: "workspace",
        // a small badge on each icon: its workspace number, and "monitor:workspace" for apps on another monitor
        badge: true,
        badgePosition: "bottomRight",     // "bottomRight" | "bottomLeft" | "topRight" | "topLeft"
        badgeSize: 9,                     // font pixel size of the badge
        // icon size as a part of the bar's width (0.3 - 0.9)
        iconScale: 0.62,
        // spacing between icons in pixels
        spacing: 4,
        // the rounded background behind the list
        background: true,
        backgroundOpacity: 1,
        // how a minimised app is drawn (0 - 1)
        minimisedOpacity: 0.45,
        // the mark on the focused app: "bar" | "dot" | "glow" | "none"
        indicator: "bar",
        // highlight behind the focused app
        highlightFocused: true,
        // left click: "focusOrMinimise" (click the focused one to minimise) | "focus"
        click: "focusOrMinimise",
        // middle click: "close" | "minimise" | "none"
        middleClick: "close",
        // mouse wheel over the list: "cycle" | "none"
        scroll: "cycle",
        // the tooltip: "title" | "app" | "titleAndPlace" | "none"
        tooltip: "titleAndPlace",
        // apps never listed (app ids, lower case)
        exclude: ["qs", "qsw", "quickshell"],
        // most icons shown (0 = no limit)
        maxApps: 0,
        // sort order: "opened" | "workspace" | "app"
        sort: "workspace"
    })

    property var settings: defaults
    function opt(key: string): var {
        return settings[key] === undefined ? defaults[key] : settings[key];
    }

    readonly property string settingsPath: `${(Quickshell.env("USERPROFILE") || "").replace(/\\/g, "/")}/.config/caelestia/taskbar.json`

    FileView {
        id: settingsFile

        path: root.settingsPath
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            try {
                root.settings = Object.assign({}, root.defaults, JSON.parse(text()));
            } catch (e) {
                console.warn(`taskbar.json: ${e} - using the defaults`);
                root.settings = root.defaults;
            }
        }
        onLoadFailed: err => {
            // no taskbar.json yet: write one with every option, so it can be edited
                setText(JSON.stringify(root.defaults, null, 2) + "\n");
        }
    }

    // ------------------------------------------------------------ where each window lives
    readonly property var barScreen: (QsWindow.window as QsWindow)?.screen ?? null
    readonly property var barMonitor: barScreen ? Hyprland.monitorFor(barScreen) : null

    // the window manager's events (workspace switch, windows opening / moving / closing, the canvas) re-read where
    // every window is: nothing runs between events
    property int tick: 0
    readonly property var refreshEvents: ["workspace", "workspacev2", "focusedmon", "focusedmonv2", "openwindow", "closewindow", "movewindow", "movewindowv2", "activewindowv2"]

    Connections {
        target: Hyprland

        function onRawEvent(event: var): void {
            if (!root.refreshEvents.includes(event.name))
                return;
            Hyprland.refreshToplevels();
            root.tick++;
            settle.restart();
        }
    }

    // the toplevel details arrive a moment after the event; then ask the window manager itself where every window is
    Timer {
        id: settle

        interval: 120
        onTriggered: {
            root.tick++;
            if (root.opt("show") === "workspace") {
                clientsProc.running = true;
                monitorsProc.running = true;
            }
        }
    }

    // "workspace": the window manager's own lists (hyprctl -j clients / monitors), read only after its events
    readonly property string hyprctl: (Quickshell.env("CAELESTIA_PACKAGE_DIR") || Quickshell.shellPath("..")).replace(/\\/g, "/") + "/qs/hyprctl.exe"
    property var wmClients: []
    property var wmMonitors: []

    Process {
        id: clientsProc

        command: [root.hyprctl, "-j", "clients"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    root.wmClients = JSON.parse(text);
                } catch (e) {
                    root.wmClients = [];
                }
            }
        }
    }

    Process {
        id: monitorsProc

        command: [root.hyprctl, "-j", "monitors"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    root.wmMonitors = JSON.parse(text);
                } catch (e) {
                    root.wmMonitors = [];
                }
            }
        }
    }

    Component.onCompleted: settle.restart()

    // the workspace this bar's monitor shows (the window manager's monitor at this screen's position)
    readonly property int shownWorkspace: {
        const sc = root.barScreen;
        const m = sc ? root.wmMonitors.find(x => x.x === sc.x && x.y === sc.y) : null;
        return m?.activeWorkspace?.id ?? -1;
    }

    function onShownWorkspace(t: var): bool {
        const cls = (t.appId || "").toLowerCase();
        return root.wmClients.some(c => !c.hidden && c.workspace?.id === root.shownWorkspace && (c.class || "").toLowerCase() === cls && c.title === t.title);
    }

    // wayland toplevel -> its Hyprland toplevel (workspace, monitor)
    readonly property var places: {
        void root.tick;
        const map = new Map();
        for (const t of Hyprland.toplevels.values) {
            if (!t?.wayland)
                continue;
            const at = t.lastIpcObject?.at ?? [0, 0];
            map.set(t.wayland, {
                monitor: t.monitor,
                monitorId: t.monitor?.id ?? -1,
                workspace: t.lastIpcObject?.workspace?.id ?? t.workspace?.id ?? -1,
                // a window of a hidden workspace is parked far off-screen by the tiler
                shown: at[0] > -10000 && at[1] > -10000
            });
        }
        return map;
    }

    function placeOf(t: var): var {
        return places.get(t) ?? { monitor: null, monitorId: -1, workspace: -1, shown: true };
    }

    readonly property var windows: {
        const exclude = (opt("exclude") ?? []).map(x => String(x).toLowerCase());
        let list = ToplevelManager.toplevels.values.filter(t => t && t.appId && !exclude.includes(t.appId.toLowerCase()) && !t.parent);
        const show = opt("show");
        if (show === "monitor") {
            const mine = barMonitor?.id ?? -1;
            list = list.filter(t => {
                const p = placeOf(t);
                return p.monitorId === -1 || p.monitorId === mine;
            });
        } else if (show === "workspace") {
            // only what the window manager shows on this bar's monitor now (the canvas included while it is shown);
            // without a window manager every window is listed
            if (root.wmClients.length > 0)
                list = list.filter(t => !t.minimized && root.onShownWorkspace(t));
        }
        const sort = opt("sort");
        if (sort === "workspace")
            list = list.slice().sort((a, b) => {
                const pa = placeOf(a), pb = placeOf(b);
                return pa.monitorId - pb.monitorId || pa.workspace - pb.workspace;
            });
        else if (sort === "app")
            list = list.slice().sort((a, b) => a.appId.localeCompare(b.appId));
        const max = opt("maxApps");
        return max > 0 ? list.slice(0, max) : list;
    }

    function groupOf(t: var): string {
        const p = placeOf(t);
        const g = opt("group");
        if (g === "workspace")
            return `${p.monitorId}:${p.workspace}`;
        if (g === "monitor")
            return `${p.monitorId}`;
        return "";
    }

    function badgeOf(t: var): string {
        const p = placeOf(t);
        if (p.workspace < 1)
            return "";
        const other = p.monitorId !== -1 && barMonitor && p.monitorId !== barMonitor.id;
        return other ? `${p.monitorId + 1}:${p.workspace}` : `${p.workspace}`;
    }

    readonly property int iconSize: Math.round(Tokens.sizes.bar.innerWidth * Math.max(0.3, Math.min(0.9, opt("iconScale"))))

    visible: windows.length > 0
    implicitWidth: Tokens.sizes.bar.innerWidth
    implicitHeight: windows.length > 0 ? column.implicitHeight + Tokens.padding.small * 2 : 0

    Behavior on implicitHeight {
        Anim {}
    }
    radius: Tokens.rounding.full
    color: opt("background") ? Qt.alpha(Colours.tPalette.m3surfaceContainer, opt("backgroundOpacity")) : "transparent"

    function cycle(step: int): void {
        if (!windows.length) return;
        const i = windows.findIndex(w => w.activated);
        windows[((i < 0 ? 0 : i + step) % windows.length + windows.length) % windows.length].activate();
    }

    WheelHandler {
        enabled: root.opt("scroll") === "cycle"
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: e => root.cycle(e.angleDelta.y < 0 ? 1 : -1)
    }

    Column {
        id: column

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Tokens.padding.small
        spacing: root.opt("spacing")

        Repeater {
            model: ScriptModel {
                values: root.windows
            }

            Item {
                id: app

                required property var modelData
                required property int index

                readonly property bool newGroup: index > 0 && root.opt("group") !== "none" && root.groupOf(modelData) !== root.groupOf(root.windows[index - 1])
                readonly property string badge: root.opt("badge") ? root.badgeOf(modelData) : ""

                implicitWidth: root.iconSize + Tokens.padding.small
                implicitHeight: root.iconSize + Tokens.padding.small + (newGroup ? 8 : 0)

                // pops in when it appears (eg. after switching workspace), one after another
                transformOrigin: Item.Center
                scale: 0.5
                opacity: 0
                Component.onCompleted: popIn.start()

                SequentialAnimation {
                    id: popIn

                    PauseAnimation {
                        duration: Math.max(0, Math.min(app.index, 8)) * 35   // index is -1 while the app is being removed
                    }
                    ParallelAnimation {
                        Anim {
                            target: app
                            property: "scale"
                            to: 1
                            easing: Tokens.anim.standardDecel
                        }
                        Anim {
                            target: app
                            property: "opacity"
                            to: 1
                        }
                    }
                }

                // the gap between two workspaces / monitors
                StyledRect {
                    visible: app.newGroup
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 1
                    implicitWidth: parent.width * 0.5
                    implicitHeight: 2
                    radius: 1
                    color: Colours.palette.m3outlineVariant
                }

                Item {
                    id: cell

                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: root.iconSize + Tokens.padding.small

                    StyledRect {
                        anchors.fill: parent
                        radius: Tokens.rounding.medium
                        color: root.opt("highlightFocused") && app.modelData.activated ? Colours.palette.m3secondaryContainer : "transparent"
                        opacity: hover.hovered && !app.modelData.activated ? 0.5 : 1
                    }

                    IconImage {
                        anchors.centerIn: parent
                        implicitSize: root.iconSize
                        source: (Quickshell.iconPath(DesktopEntries.heuristicLookup(app.modelData.appId)?.icon ?? app.modelData.appId, true) || Quickshell.shellPath("assets/app-generic.svg"))

                        onStatusChanged: if (status === Image.Error && source != Quickshell.shellPath("assets/app-generic.svg")) source = Quickshell.shellPath("assets/app-generic.svg")
                        opacity: app.modelData.minimized ? root.opt("minimisedOpacity") : 1
                    }

                    // focused mark
                    StyledRect {
                        visible: app.modelData.activated && root.opt("indicator") === "bar"
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: -Tokens.padding.extraSmall
                        implicitWidth: 3
                        implicitHeight: parent.height * 0.5
                        radius: 2
                        color: Colours.palette.m3primary
                    }
                    StyledRect {
                        visible: app.modelData.activated && root.opt("indicator") === "dot"
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: -Tokens.padding.extraSmall
                        implicitWidth: 5
                        implicitHeight: 5
                        radius: 3
                        color: Colours.palette.m3primary
                    }
                    StyledRect {
                        visible: app.modelData.activated && root.opt("indicator") === "glow"
                        anchors.fill: parent
                        radius: Tokens.rounding.medium
                        color: "transparent"
                        border.width: 2
                        border.color: Colours.palette.m3primary
                    }

                    // where it lives
                    StyledRect {
                        visible: app.badge !== ""
                        readonly property string pos: root.opt("badgePosition")
                        anchors.right: pos.endsWith("Right") ? parent.right : undefined
                        anchors.left: pos.endsWith("Left") ? parent.left : undefined
                        anchors.bottom: pos.startsWith("bottom") ? parent.bottom : undefined
                        anchors.top: pos.startsWith("top") ? parent.top : undefined
                        implicitWidth: Math.max(badgeText.implicitWidth + 4, badgeText.implicitHeight)
                        implicitHeight: badgeText.implicitHeight
                        radius: implicitHeight / 2
                        color: Colours.palette.m3primaryContainer

                        StyledText {
                            id: badgeText

                            anchors.centerIn: parent
                            text: app.badge
                            font.pixelSize: root.opt("badgeSize")
                            color: Colours.palette.m3onPrimaryContainer
                        }
                    }

                    HoverHandler {
                        id: hover
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: {
                            if (root.opt("click") === "focusOrMinimise" && app.modelData.activated && !app.modelData.minimized)
                                app.modelData.minimized = true;
                            else
                                app.modelData.activate();
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.MiddleButton
                        onTapped: {
                            const action = root.opt("middleClick");
                            if (action === "close")
                                app.modelData.close();
                            else if (action === "minimise")
                                app.modelData.minimized = true;
                        }
                    }

                    ToolTip.visible: hover.hovered && root.opt("tooltip") !== "none"
                    ToolTip.delay: 400
                    ToolTip.text: {
                        const t = root.opt("tooltip");
                        const name = app.modelData.title || app.modelData.appId;
                        if (t === "app")
                            return app.modelData.appId;
                        if (t === "titleAndPlace") {
                            const p = root.placeOf(app.modelData);
                            const where = p.workspace > 0 ? `\nworkspace ${p.workspace}${p.monitor ? " on " + p.monitor.name : ""}` : "";
                            return name + where;
                        }
                        return name;
                    }
                }
            }
        }
    }
}
