pragma ComponentBehavior: Bound

// Waybar for Windows, built in Quickshell: reads Waybar's own config and style.css from
//   %USERPROFILE%\.config\waybar\config.jsonc   (or "config")   and   %USERPROFILE%\.config\waybar\style.css
// Supported: position/height/width/spacing/margin/output/layer, modules-left/center/right with clock,
// (hyprland|sway|wlr|niri)/workspaces, (hyprland|sway)/window, tray, wlr/taskbar, cpu, memory, temperature,
// battery, network, pulseaudio/wireplumber, backlight, disk, custom/<name> (exec, interval, return-type json,
// on-click...). style.css: colours (+ @define-color, alpha(), Caelestia's @primary/@surface/@on_surface...),
// background, color, border, border-bottom, border-radius, padding, margin, font-family/-size/-weight, opacity.
// Saving either file reloads the bar. On/off: Settings -> Windows (or Setup).
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.services
import qs.utils

Scope {
    id: root

    readonly property string dir: `${Paths.home}/.config/waybar`
    property var bars: []
    property var css: ({ rules: [], colors: {} })
    property string error: ""

    function load(): void {
        const txt = cfgFile.loaded ? cfgFile.text() : (cfgFallback.loaded ? cfgFallback.text() : "");
        if (!txt) return;
        try {
            bars = WaybarStyle.parseConfig(txt);
            error = "";
        } catch (e) {
            error = String(e);
            console.warn("Waybar: config.jsonc could not be read:", e);
        }
    }

    FileView {
        id: cfgFile

        path: `${root.dir}/config.jsonc`
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.load()
        onLoadFailed: err => {
            if (err === FileViewError.FileNotFound && !cfgFallback.loaded)
                setText(root.defaultConfig);   // first run: a starting point to edit
        }
    }

    FileView {
        id: cfgFallback

        path: `${root.dir}/config`
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: if (!cfgFile.loaded) root.load()
    }

    FileView {
        id: cssFile

        path: `${root.dir}/style.css`
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.css = WaybarStyle.parseCss(text())
        onLoadFailed: err => {
            if (err === FileViewError.FileNotFound) setText(root.defaultStyle);
        }
    }

    Instantiator {
        model: root.bars

        delegate: Variants {
            id: barDef

            required property var modelData
            readonly property var outputs: modelData.output === undefined ? null : [].concat(modelData.output)

            model: Quickshell.screens.filter(s => !barDef.outputs || barDef.outputs.some(o => o === s.name || (o.startsWith("!") && o.slice(1) !== s.name)))

            PanelWindow {
                id: bar

                required property var modelData
                readonly property var conf: barDef.modelData
                readonly property string position: conf.position ?? "top"
                readonly property bool vertical: position === "left" || position === "right"
                readonly property string posClass: position
                readonly property var wst: WaybarStyle.style(root.css, [{ tag: "window", id: "waybar", classes: [position] }])
                readonly property var allst: WaybarStyle.style(root.css, [{ tag: "*" }])
                readonly property real barThickness: vertical ? (conf.width ?? 40) : (conf.height ?? 34)
                readonly property real windowPad: 0
                readonly property color textColor: WaybarStyle.color(root.css, wst["color"] ?? allst["color"]) ?? Colours.palette.m3onSurface
                readonly property string fontFamily: allst["font-family"] ?? wst["font-family"] ?? "Segoe UI, Material Symbols Rounded"
                readonly property real fontSize: WaybarStyle.px(allst["font-size"] ?? wst["font-size"], 13)
                readonly property string fontWeight: allst["font-weight"] ?? wst["font-weight"] ?? "normal"
                readonly property var m: WaybarStyle.box(conf.margin ?? `${conf["margin-top"] ?? 0} ${conf["margin-right"] ?? 0} ${conf["margin-bottom"] ?? 0} ${conf["margin-left"] ?? 0}`)

                screen: modelData
                color: "transparent"
                anchors.top: position !== "bottom"
                anchors.bottom: position !== "top"
                anchors.left: position !== "right"
                anchors.right: position !== "left"
                margins.top: m.t
                margins.bottom: m.b
                margins.left: m.l
                margins.right: m.r
                implicitHeight: vertical ? 0 : barThickness
                implicitWidth: vertical ? barThickness : 0
                exclusiveZone: conf.exclusive === false ? 0 : barThickness
                WlrLayershell.layer: conf.layer === "bottom" ? WlrLayer.Bottom : conf.layer === "overlay" ? WlrLayer.Overlay : WlrLayer.Top
                WlrLayershell.namespace: "waybar"

                Rectangle {
                    anchors.fill: parent
                    color: WaybarStyle.color(root.css, bar.wst["background"] ?? bar.wst["background-color"]) ?? Qt.alpha(Colours.palette.m3surface, 0.85)
                    radius: WaybarStyle.px(bar.wst["border-radius"], 0)
                    border.width: bar.wst["border"] ? WaybarStyle.px(bar.wst["border"].split(" ")[0], 0) : 0
                    border.color: bar.wst["border"] ? (WaybarStyle.color(root.css, bar.wst["border"].split(" ").slice(2).join(" ")) ?? "transparent") : "transparent"
                    opacity: bar.wst["opacity"] !== undefined ? parseFloat(bar.wst["opacity"]) : 1
                }

                Repeater {
                    model: ["left", "center", "right"]

                    GridLayout {
                        id: group

                        required property string modelData
                        readonly property var gst: WaybarStyle.style(root.css, [{ tag: "window", id: "waybar", classes: [bar.position] }, { classes: ["modules-" + modelData] }])
                        readonly property var gm: WaybarStyle.box(gst["margin"])

                        flow: bar.vertical ? GridLayout.TopToBottom : GridLayout.LeftToRight
                        rowSpacing: bar.conf.spacing ?? 4
                        columnSpacing: bar.conf.spacing ?? 4

                        x: bar.vertical ? (parent.width - implicitWidth) / 2
                                        : modelData === "left" ? gm.l : modelData === "right" ? parent.width - implicitWidth - gm.r : (parent.width - implicitWidth) / 2
                        y: !bar.vertical ? (parent.height - implicitHeight) / 2
                                         : modelData === "left" ? gm.t : modelData === "right" ? parent.height - implicitHeight - gm.b : (parent.height - implicitHeight) / 2

                        Repeater {
                            model: bar.conf["modules-" + group.modelData] ?? []

                            WaybarModule {
                                required property var modelData

                                name: modelData
                                cfg: bar.conf[modelData] ?? {}
                                css: root.css
                                side: group.modelData
                                vertical: bar.vertical
                                screen: bar.modelData
                                barWindow: bar
                            }
                        }
                    }
                }
            }
        }
    }

    readonly property string defaultConfig: `// Waybar for Windows (Caelestia). Same format as Waybar on Linux: https://github.com/Alexays/Waybar/wiki
// Save this file and the bar reloads. Commands (on-click, exec) run with cmd /c.
{
    "position": "top",
    "height": 34,
    "spacing": 6,
    "margin": "6 8 0 8",
    "modules-left": ["hyprland/workspaces", "wlr/taskbar"],
    "modules-center": ["clock"],
    "modules-right": ["tray", "pulseaudio", "network", "cpu", "memory", "battery"],

    "hyprland/workspaces": { "format": "{name}" },
    "wlr/taskbar": { "icon-size": 18 },
    "clock": { "format": "{:%a %d %b  %H:%M}", "tooltip-format": "{:%A %d %B %Y}" },
    "tray": { "icon-size": 16, "spacing": 8 },
    "pulseaudio": { "format": "{icon} {volume}%", "format-muted": " muted", "format-icons": { "default": ["", "", ""] } },
    "network": { "format-wifi": " {essid}", "format-ethernet": " wired", "format-disconnected": " offline" },
    "cpu": { "format": " {usage}%", "interval": 2 },
    "memory": { "format": " {percentage}%", "interval": 4 },
    "battery": { "format": "{icon} {capacity}%", "format-icons": ["", "", "", "", ""], "states": { "warning": 25, "critical": 10 } }
}
`

    readonly property string defaultStyle: `/* Waybar for Windows (Caelestia). Waybar's CSS: #module-name for each module.
   @primary, @surface, @surface_container, @on_surface, @secondary_container ... are Caelestia's live colours. */
* {
    font-family: "Segoe UI", "Material Symbols Rounded", "Symbols Nerd Font", sans-serif;
    font-size: 13px;
}

window#waybar {
    background: alpha(@surface, 0.85);
    color: @on_surface;
    border-radius: 14px;
}

#workspaces button {
    padding: 2px 10px;
    margin: 0 2px;
    border-radius: 10px;
    color: @on_surface_variant;
}

#workspaces button.active {
    background: @primary;
    color: @on_primary;
}

#clock, #pulseaudio, #network, #cpu, #memory, #battery, #tray, #taskbar {
    padding: 2px 10px;
    border-radius: 10px;
    background: @surface_container_high;
}

#battery.warning { color: @tertiary; }
#battery.critical { color: @error; }
`
}
