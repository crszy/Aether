pragma Singleton

import QtQuick
import Quickshell

Singleton {
    id: root

    readonly property string desktop: (Quickshell.env("XDG_CURRENT_DESKTOP") || "").split(":")[0].toLowerCase()

    readonly property bool tiling: {
        if (Quickshell.env("NIRI_SOCKET"))
            return true;
        if (Quickshell.env("HYPRLAND_INSTANCE_SIGNATURE"))
            return true;
        if (Quickshell.env("SWAYSOCK"))
            return true;
        if (Quickshell.env("MANGO_INSTANCE_SIGNATURE"))
            return true;
        if (Quickshell.env("MIRACLESOCK"))
            return true;

        switch (desktop) {
        case "niri":
        case "hyprland":
        case "sway":
        case "scroll":
        case "mango":
        case "miracle-wm":
        case "umbriel":
        case "river":
        case "dwl":
            return true;
        default:
            return false;
        }
    }

    readonly property bool supportsMinimize: !tiling
}
