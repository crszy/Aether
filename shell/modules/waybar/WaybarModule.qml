pragma ComponentBehavior: Bound

// One Waybar module, drawn from its config (format, format-icons, on-click ...) and style.css (#name ...).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Widgets
import Quickshell.Hyprland
import Quickshell.Services.SystemTray
import Quickshell.Services.UPower
import Caelestia.Config
import Caelestia.Services
import qs.components
import qs.services
import qs.utils

Item {
    id: root

    required property string name          // "clock", "hyprland/workspaces", "custom/weather" ...
    required property var cfg              // this module's config object (may be {})
    required property var css
    required property string side          // "left" | "center" | "right"
    required property bool vertical
    required property var screen
    required property var barWindow

    readonly property string kind: name.split("#")[0]           // "clock#2" -> "clock"
    readonly property string cssId: kind.replace(/^(hyprland|sway|wlr|niri|river)\//, "").replace("/", "-")
    readonly property var chain: [{ tag: "window", id: "waybar", classes: [barWindow.posClass] }, { classes: ["modules-" + side] }, { id: cssId, classes: stateClasses }]
    property list<string> stateClasses: []
    readonly property var st: WaybarStyle.style(css, chain)
    readonly property var pad: WaybarStyle.box(st["padding"] ?? "0 8px")
    readonly property var mar: WaybarStyle.box(st["margin"])

    property date now: new Date()
    property var values: ({})
    property string text: ""
    property string tooltip: ""
    property bool hidden: false

    visible: !hidden && (text.length > 0 || content.item !== null && isRich)
    readonly property bool isRich: ["workspaces", "tray", "taskbar"].includes(cssId)

    implicitWidth: bg.implicitWidth + mar.l + mar.r
    implicitHeight: bg.implicitHeight + mar.t + mar.b

    function fmt(key: string, fallback: string): string {
        return WaybarStyle.format(cfg[key] ?? fallback, values, now);
    }

    function click(button: int): void {
        const key = button === Qt.RightButton ? "on-click-right" : button === Qt.MiddleButton ? "on-click-middle" : "on-click";
        if (cfg[key]) WaybarStyle.run(cfg[key]);
        else if (button === Qt.LeftButton) defaultClick();
    }

    function defaultClick(): void {
        if (cssId === "pulseaudio" || cssId === "wireplumber") { if (Audio.sink?.audio) Audio.sink.audio.muted = !Audio.sink.audio.muted; }
    }

    function scroll(up: bool): void {
        const key = up ? "on-scroll-up" : "on-scroll-down";
        if (cfg[key]) return WaybarStyle.run(cfg[key]);
        if (cssId === "pulseaudio" || cssId === "wireplumber") up ? Audio.incrementVolume(0.05) : Audio.decrementVolume(0.05);
        else if (cssId === "backlight") { const m = Brightness.getMonitorForScreen(root.screen); if (m) m.setBrightness(m.brightness + (up ? 0.05 : -0.05)); }
    }

    // ---------------- values per module ----------------
    function refresh(): void {
        now = new Date();
        switch (cssId) {
        case "clock":
            text = fmt("format", "{:%H:%M}");
            tooltip = cfg["tooltip-format"] ? WaybarStyle.format(cfg["tooltip-format"].replace(/<[^>]+>/g, ""), {}, now) : "";
            break;
        case "cpu": {
            const u = Math.round(Cpu.percentage * 100);
            values = { usage: u, load: u, icon: WaybarStyle.icon(cfg["format-icons"], u, "") };
            text = fmt("format", "{usage}% ");
            break;
        }
        case "memory": {
            const tot = Memory.total / 1048576, used = Memory.used / 1048576;
            const pc = Math.round(Memory.percentage * 100);
            values = { percentage: pc, used: used.toFixed(1), total: tot.toFixed(1), avail: (tot - used).toFixed(1), icon: WaybarStyle.icon(cfg["format-icons"], pc, "") };
            text = fmt("format", "{}% ".replace("{}", "{percentage}"));
            break;
        }
        case "temperature": {
            const t = Math.round(Cpu.temperature || 0);
            values = { temperatureC: t, temperatureF: Math.round(t * 9 / 5 + 32), icon: WaybarStyle.icon(cfg["format-icons"], t, "") };
            hidden = t <= 0;
            text = fmt(t >= (cfg["critical-threshold"] ?? 999) ? "format-critical" : "format", "{temperatureC}°C");
            stateClasses = t >= (cfg["critical-threshold"] ?? 999) ? ["critical"] : [];
            break;
        }
        case "battery": {
            const d = UPower.displayDevice;
            if (!d || !d.isLaptopBattery) { hidden = true; break; }
            hidden = false;
            const cap = Math.round(d.percentage * 100);
            const charging = d.state === UPowerDeviceState.Charging;
            const full = d.state === UPowerDeviceState.FullyCharged;
            const secs = charging ? d.timeToFull : d.timeToEmpty;
            values = { capacity: cap, icon: WaybarStyle.icon(cfg["format-icons"], cap, ""), time: secs > 0 ? `${Math.floor(secs / 3600)}h ${Math.floor(secs / 60) % 60}m` : "" };
            const states = cfg.states ?? {};
            const st2 = Object.keys(states).filter(k => cap <= states[k]).sort((a, b) => states[a] - states[b])[0];
            stateClasses = [charging ? "charging" : full ? "full" : "discharging"].concat(st2 ? [st2] : []);
            text = fmt(charging && cfg["format-charging"] ? "format-charging" : full && cfg["format-full"] ? "format-full" : st2 && cfg[`format-${st2}`] ? `format-${st2}` : "format", "{capacity}% {icon}");
            break;
        }
        case "network": {
            const a = Nmcli.active;
            if (a) {
                values = { essid: a.ssid, signalStrength: a.strength, frequency: (a.frequency / 1000).toFixed(1), ifname: "Wi-Fi", icon: WaybarStyle.icon(cfg["format-icons"], a.strength, "wifi") };
                stateClasses = ["wifi"];
                text = fmt(cfg["format-wifi"] ? "format-wifi" : "format", "{essid} ({signalStrength}%) ");
            } else if (Nmcli.activeEthernet) {
                values = { ifname: "Ethernet", icon: WaybarStyle.icon(cfg["format-icons"], 100, "ethernet") };
                stateClasses = ["ethernet"];
                text = fmt(cfg["format-ethernet"] ? "format-ethernet" : "format", "{ifname} ");
            } else {
                values = { icon: WaybarStyle.icon(cfg["format-icons"], 0, "disconnected") };
                stateClasses = ["disconnected"];
                text = fmt("format-disconnected", "Disconnected ⚠");
            }
            break;
        }
        case "pulseaudio":
        case "wireplumber": {
            const v = Math.round(Audio.volume * 100);
            const muted = Audio.muted;
            values = { volume: v, icon: WaybarStyle.icon(cfg["format-icons"], v, "default"), format_source: "" };
            stateClasses = muted ? ["muted"] : [];
            text = fmt(muted && cfg["format-muted"] ? "format-muted" : "format", "{volume}% {icon}");
            break;
        }
        case "backlight": {
            const m = Brightness.getMonitorForScreen(root.screen);
            const pc = Math.round((m?.brightness ?? 0) * 100);
            hidden = !m;
            values = { percent: pc, icon: WaybarStyle.icon(cfg["format-icons"], pc, "") };
            text = fmt("format", "{percent}% {icon}");
            break;
        }
        case "disk": {
            const pc = Math.round(Storage.percentage * 100);
            values = { percentage_used: pc, percentage_free: 100 - pc, path: "C:" };
            text = fmt("format", "{percentage_used}%");
            break;
        }
        case "window": {
            const t = ToplevelManager.activeToplevel;
            const title = t?.title ?? "";
            const max = cfg["max-length"] ?? 60;
            values = { title: title.length > max ? title.slice(0, max - 1) + "…" : title, app_id: t?.appId ?? "", class: t?.appId ?? "" };
            values[""] = values.title;
            text = fmt("format", "{title}");
            break;
        }
        default:
            break;
        }
    }

    Timer {
        running: root.cssId !== "custom"
        repeat: true
        triggeredOnStart: true
        interval: Math.max(250, (root.cfg.interval ?? (root.cssId === "clock" ? 1 : 2)) * 1000)
        onTriggered: root.refresh()
    }

    ServiceRef { service: root.cssId === "cpu" || root.cssId === "temperature" ? Cpu : null }
    ServiceRef { service: root.cssId === "memory" ? Memory : null }
    ServiceRef { service: root.cssId === "disk" ? Storage : null }

    // ---------------- custom/<name>: run `exec`, show its output (plain text or return-type json) ----------------
    Process {
        id: customProc

        command: root.cssId === "custom" && root.cfg.exec ? (WaybarStyle.isList(root.cfg.exec) ? root.cfg.exec : ["cmd.exe", "/c", String(root.cfg.exec)]) : []
        stdout: StdioCollector {
            onStreamFinished: {
                const out = text.trim();
                if (root.cfg["return-type"] === "json") {
                    try {
                        const j = JSON.parse(out.split("\n").pop());
                        root.values = { "": j.text ?? "", text: j.text ?? "", alt: j.alt ?? "", percentage: j.percentage ?? 0,
                                        icon: WaybarStyle.icon(root.cfg["format-icons"], j.percentage ?? 0, j.alt ?? "") };
                        root.tooltip = j.tooltip ?? "";
                        root.stateClasses = j["class"] ? [].concat(j["class"]) : [];
                    } catch (e) {
                        root.values = { "": out };
                    }
                } else {
                    const lines = out.split("\n");
                    root.values = { "": lines[0] ?? "", text: lines[0] ?? "" };
                    root.tooltip = lines[1] ?? "";
                }
                root.text = root.values[""] ? WaybarStyle.format(root.cfg.format ?? "{}", root.values, new Date()) : "";
            }
        }
    }

    Timer {
        running: root.cssId === "custom" && !!root.cfg.exec
        repeat: root.cfg.interval !== "once"
        triggeredOnStart: true
        interval: Math.max(500, (typeof root.cfg.interval === "number" ? root.cfg.interval : 5) * 1000)
        onTriggered: if (!customProc.running) customProc.running = true
    }

    Component.onCompleted: if (cssId === "custom" && !cfg.exec) text = cfg.format ?? ""

    // ---------------- drawing ----------------
    Rectangle {
        id: bg

        x: root.mar.l
        y: root.mar.t
        implicitWidth: (root.isRich ? content.implicitWidth : label.implicitWidth) + root.pad.l + root.pad.r
        implicitHeight: Math.max(root.barWindow.barThickness - root.mar.t - root.mar.b - root.barWindow.windowPad * 2, (root.isRich ? content.implicitHeight : label.implicitHeight) + root.pad.t + root.pad.b)
        color: WaybarStyle.color(root.css, root.st["background"] ?? root.st["background-color"]) ?? "transparent"
        radius: WaybarStyle.px(root.st["border-radius"], 0)
        border.width: root.st["border"] ? WaybarStyle.px(root.st["border"].split(" ")[0], 0) : 0
        border.color: root.st["border"] ? (WaybarStyle.color(root.css, root.st["border"].split(" ").slice(2).join(" ")) ?? "transparent") : "transparent"
        opacity: root.st["opacity"] !== undefined ? parseFloat(root.st["opacity"]) : 1

        Rectangle {    // border-bottom underline, a favourite of Waybar themes
            visible: !!root.st["border-bottom"]
            anchors.bottom: parent.bottom
            width: parent.width
            height: root.st["border-bottom"] ? WaybarStyle.px(root.st["border-bottom"].split(" ")[0], 0) : 0
            color: root.st["border-bottom"] ? (WaybarStyle.color(root.css, root.st["border-bottom"].split(" ").slice(2).join(" ")) ?? "transparent") : "transparent"
        }

        Row {
            id: label

            visible: !root.isRich
            anchors.centerIn: parent

            Repeater {
                model: WaybarStyle.runs(root.text, root.st["font-family"] ?? root.barWindow.fontFamily)

                Text {
                    required property var modelData

                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.text
                    textFormat: Text.PlainText
                    color: WaybarStyle.color(root.css, root.st["color"]) ?? root.barWindow.textColor
                    font.family: modelData.font
                    font.pixelSize: WaybarStyle.px(root.st["font-size"], root.barWindow.fontSize)
                    font.bold: (root.st["font-weight"] ?? root.barWindow.fontWeight) === "bold" || parseInt(root.st["font-weight"] ?? root.barWindow.fontWeight) >= 600
                }
            }
        }

        Loader {
            id: content

            active: root.isRich
            anchors.centerIn: parent
            sourceComponent: root.cssId === "workspaces" ? workspacesComp : root.cssId === "tray" ? trayComp : root.cssId === "taskbar" ? taskbarComp : null
        }

        MouseArea {
            visible: !root.isRich
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            hoverEnabled: true
            onClicked: e => root.click(e.button)
            onWheel: e => root.scroll(e.angleDelta.y > 0)
            ToolTip.visible: containsMouse && root.tooltip.length > 0 && root.cfg.tooltip !== false
            ToolTip.delay: 500
            ToolTip.text: root.tooltip
        }
    }

    // workspaces: one button per workspace (Windows virtual desktops through the port's Hyprland module)
    Component {
        id: workspacesComp

        GridLayout {
            flow: root.vertical ? GridLayout.TopToBottom : GridLayout.LeftToRight
            rowSpacing: 0
            columnSpacing: 0

            Repeater {
                model: ScriptModel {
                    values: Hyprland.workspaces.values.filter(w => w.id > 0)
                }

                Rectangle {
                    id: wsb

                    required property var modelData
                    readonly property bool active: modelData.id === Hyprland.focusedWorkspace?.id
                    readonly property var bst: WaybarStyle.style(root.css, root.chain.concat([{ tag: "button", classes: active ? ["active", "focused"] : [] }]))
                    readonly property var bpad: WaybarStyle.box(bst["padding"] ?? "0 6px")
                    readonly property var bmar: WaybarStyle.box(bst["margin"])

                    Layout.leftMargin: bmar.l
                    Layout.rightMargin: bmar.r
                    Layout.topMargin: bmar.t
                    Layout.bottomMargin: bmar.b
                    implicitWidth: wsl.implicitWidth + bpad.l + bpad.r
                    implicitHeight: wsl.implicitHeight + bpad.t + bpad.b
                    color: WaybarStyle.color(root.css, bst["background"] ?? bst["background-color"]) ?? (active ? Qt.alpha(Colours.palette.m3primary, 0.25) : "transparent")
                    radius: WaybarStyle.px(bst["border-radius"], 0)

                    Rectangle {
                        visible: !!wsb.bst["border-bottom"]
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: wsb.bst["border-bottom"] ? WaybarStyle.px(wsb.bst["border-bottom"].split(" ")[0], 0) : 0
                        color: wsb.bst["border-bottom"] ? (WaybarStyle.color(root.css, wsb.bst["border-bottom"].split(" ").slice(2).join(" ")) ?? "transparent") : "transparent"
                    }

                    Row {
                        id: wsl

                        anchors.centerIn: parent

                        Repeater {
                            model: WaybarStyle.runs(WaybarStyle.format(root.cfg.format ?? "{name}", {
                                name: wsb.modelData.name || String(wsb.modelData.id), id: wsb.modelData.id, "": wsb.modelData.name,
                                icon: WaybarStyle.icon(root.cfg["format-icons"], 0, wsb.active ? "active" : (root.cfg["format-icons"]?.[wsb.modelData.name] !== undefined ? wsb.modelData.name : String(wsb.modelData.id)))
                            }, new Date()), wsb.bst["font-family"] ?? root.barWindow.fontFamily)

                            Text {
                                required property var modelData

                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.text
                                textFormat: Text.PlainText
                                color: WaybarStyle.color(root.css, wsb.bst["color"]) ?? root.barWindow.textColor
                                font.family: modelData.font
                                font.pixelSize: WaybarStyle.px(wsb.bst["font-size"], root.barWindow.fontSize)
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: wsb.modelData.activate()
                        onWheel: e => Hyprland.dispatch(`workspace ${e.angleDelta.y > 0 ? "e-1" : "e+1"}`)
                    }
                }
            }
        }
    }

    // tray: the notification-area icons; left click activates, right click opens the app's menu
    Component {
        id: trayComp

        GridLayout {
            flow: root.vertical ? GridLayout.TopToBottom : GridLayout.LeftToRight
            rowSpacing: root.cfg.spacing ?? 6
            columnSpacing: root.cfg.spacing ?? 6

            Repeater {
                model: ScriptModel {
                    values: SystemTray.items.values.filter(i => i.status !== Status.Passive)
                }

                IconImage {
                    id: ti

                    required property var modelData

                    implicitSize: root.cfg["icon-size"] ?? Math.round(root.barWindow.fontSize * 1.4)
                    source: modelData.icon

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                        onClicked: e => {
                            if (e.button === Qt.LeftButton) ti.modelData.activate();
                            else if (e.button === Qt.MiddleButton) ti.modelData.secondaryActivate();
                            else if (ti.modelData.hasMenu) {
                                const p = ti.mapToItem(null, e.x, e.y);
                                ti.modelData.display(root.barWindow, p.x, p.y);
                            }
                        }
                    }
                }
            }
        }
    }

    // wlr/taskbar: open windows as icons (click focuses / minimises, middle-click closes)
    Component {
        id: taskbarComp

        GridLayout {
            flow: root.vertical ? GridLayout.TopToBottom : GridLayout.LeftToRight
            rowSpacing: 4
            columnSpacing: 4

            Repeater {
                model: ScriptModel {
                    values: ToplevelManager.toplevels.values.filter(t => t && t.appId && !["qs", "qsw", "quickshell"].includes(t.appId.toLowerCase()) && !t.parent)
                }

                Rectangle {
                    id: tb

                    required property var modelData
                    readonly property var bst: WaybarStyle.style(root.css, root.chain.concat([{ tag: "button", classes: modelData.activated ? ["active"] : [] }]))

                    implicitWidth: (root.cfg["icon-size"] ?? Math.round(root.barWindow.fontSize * 1.5)) + 8
                    implicitHeight: implicitWidth
                    radius: WaybarStyle.px(bst["border-radius"], 6)
                    color: WaybarStyle.color(root.css, bst["background"] ?? bst["background-color"]) ?? (modelData.activated ? Qt.alpha(Colours.palette.m3primary, 0.25) : "transparent")

                    Image {   // loaded at 48 px and scaled: some app icons fail to load at small sizes
                        anchors.centerIn: parent
                        width: parent.implicitWidth - 8
                        height: width
                        sourceSize: Qt.size(48, 48)
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                        source: (Quickshell.iconPath(DesktopEntries.heuristicLookup(tb.modelData.appId)?.icon ?? tb.modelData.appId, true) || Quickshell.shellPath("assets/app-generic.svg"))
                        onStatusChanged: if (status === Image.Error && source != Quickshell.shellPath("assets/app-generic.svg")) source = Quickshell.shellPath("assets/app-generic.svg")
                        opacity: tb.modelData.minimized ? 0.45 : 1
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                        hoverEnabled: true
                        onClicked: e => {
                            if (e.button === Qt.MiddleButton) tb.modelData.close();
                            else if (tb.modelData.activated && !tb.modelData.minimized) tb.modelData.minimized = true;
                            else tb.modelData.activate();
                        }
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 500
                        ToolTip.text: tb.modelData.title
                    }
                }
            }
        }
    }
}
