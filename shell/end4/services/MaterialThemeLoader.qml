pragma Singleton
pragma ComponentBehavior: Bound

import qs.end4.modules.common
import QtQuick
import Quickshell
import qs.services as Cae

// Windows version of end-4's MaterialThemeLoader. end-4 reads Material You colours that matugen generated from
// the wallpaper; Caelestia already makes the same Material colours from your wallpaper, so end-4 takes them
// from Caelestia's live scheme (same m3* names) and changes whenever Caelestia's do.
Singleton {
    id: root
    property string filePath: ""

    function reapplyTheme() {
        const palette = Cae.Colours.palette;
        if (!palette)
            return;
        for (const key in Appearance.m3colors) {
            if (!key.startsWith("m3") || key.endsWith("Changed") || typeof Appearance.m3colors[key] === "function")
                continue;
            const value = palette[key];
            if (value !== undefined && value !== null)
                Appearance.m3colors[key] = value;
        }
        Appearance.m3colors.darkmode = !Cae.Colours.light;
    }

    function applyColors(fileContent) {
        reapplyTheme();
    }

    function resetFilePathNextTime() {}

    // light/dark: Caelestia's own switch
    function toggleLightDark() {
        Cae.Colours.setMode(Cae.Colours.light ? "dark" : "light");
    }

    Connections {
        target: Cae.Colours
        function onPaletteChanged() {
            root.reapplyTheme();
        }
        function onLightChanged() {
            root.reapplyTheme();
        }
    }

    Connections {
        target: Cae.Colours.palette
        ignoreUnknownSignals: true
        function onM3primaryChanged() {
            root.reapplyTheme();
        }
    }

    Component.onCompleted: reapplyTheme()
}
