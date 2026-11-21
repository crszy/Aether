pragma Singleton

import ".."
import QtQuick
import Quickshell
import Caelestia.Config
import qs.services
import qs.utils

// >border: the colour of the window manager's focus border (general:col.active_border in hyprland.conf).
// Saved in hyprland.conf and sent to the running window manager with hyprctl keyword. A static colour: Windows
// draws nothing extra per frame, so it costs no CPU (animated borders would redraw all the time).
Searcher {
    id: root

    readonly property string hyprctl: (Quickshell.env("CAELESTIA_PACKAGE_DIR") || Quickshell.shellPath("..")).replace(/\\/g, "/") + "/qs/hyprctl.exe"
    readonly property string current: (HyprConf.opt("general:col.active_border", "") || "").toLowerCase()

    function transformSearch(search: string): string {
        return search.slice(`${GlobalConfig.launcher.actionPrefix}border `.length);
    }

    function hex(c: color): string {
        const h = v => Math.round(v * 255).toString(16).padStart(2, "0");
        return `${h(c.r)}${h(c.g)}${h(c.b)}`;
    }

    function apply(rgb: string): void {
        const value = `rgb(${rgb})`;
        HyprConf.setOption("general:col.active_border", value);
        Quickshell.execDetached([hyprctl, "keyword", "general:col.active_border", value]);
    }

    // ">border #ff8800" (or ff8800): a colour of your own, offered first
    function results(search: string): var {
        const typed = transformSearch(search).trim().replace(/^#/, "");
        const found = query(search);
        if (/^[0-9a-fA-F]{6}$/.test(typed)) {
            custom.rgb = typed.toLowerCase();
            return [custom].concat(found);
        }
        return found;
    }

    list: [
        Choice {
            name: qsTr("Accent")
            description: qsTr("Caelestia's accent colour (your current scheme)")
            rgb: root.hex(Colours.palette.m3primary)
        },
        Choice {
            name: qsTr("Accent soft")
            description: qsTr("The scheme's secondary colour")
            rgb: root.hex(Colours.palette.m3secondary)
        },
        Choice {
            name: qsTr("Accent tertiary")
            description: qsTr("The scheme's tertiary colour")
            rgb: root.hex(Colours.palette.m3tertiary)
        },
        Choice { name: qsTr("White"); description: "#ffffff"; rgb: "ffffff" },
        Choice { name: qsTr("Grey"); description: "#9e9e9e"; rgb: "9e9e9e" },
        Choice { name: qsTr("Red"); description: "#f38ba8"; rgb: "f38ba8" },
        Choice { name: qsTr("Orange"); description: "#fab387"; rgb: "fab387" },
        Choice { name: qsTr("Yellow"); description: "#f9e2af"; rgb: "f9e2af" },
        Choice { name: qsTr("Green"); description: "#a6e3a1"; rgb: "a6e3a1" },
        Choice { name: qsTr("Teal"); description: "#94e2d5"; rgb: "94e2d5" },
        Choice { name: qsTr("Blue"); description: "#89b4fa"; rgb: "89b4fa" },
        Choice { name: qsTr("Purple"); description: "#cba6f7"; rgb: "cba6f7" },
        Choice { name: qsTr("Pink"); description: "#f5c2e7"; rgb: "f5c2e7" }
    ]
    useFuzzy: false

    Choice {
        id: custom

        name: qsTr("This colour")
        description: `#${rgb}`
    }

    component Choice: QtObject {
        required property string name
        property string description
        property string rgb

        readonly property bool active: root.current.indexOf(rgb) >= 0

        function onClicked(list: AppList): void {
            list.screenState.launcher = false;
            root.apply(rgb);
        }
    }
}
