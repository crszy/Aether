pragma Singleton
// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
// Windows: no fonts.json; the panel uses the first installed of Google Sans Flex, Rubik, Inter, Segoe UI Variable.
import QtQuick
import Quickshell

Singleton {
    id: f
    readonly property var fams: Qt.fontFamilies()
    function has(n) { return fams.indexOf(n) >= 0 }
    function first(list, def) {
        for (let i = 0; i < list.length; i++)
            if (has(list[i]))
                return list[i];
        return def;
    }
    readonly property bool symbols: has("Material Symbols Rounded")
    readonly property string ui: first(["Google Sans Flex", "Rubik", "Inter", "Segoe UI Variable Text"], "Segoe UI")
    readonly property string title: first(["Google Sans Flex", "Rubik", "Inter", "Segoe UI Variable Display"], ui)
    readonly property string mono: first(["JetBrainsMono Nerd Font", "JetBrains Mono", "Cascadia Mono"], "Consolas")
    readonly property string icons: has("Segoe Fluent Icons") ? "Segoe Fluent Icons" : "Segoe MDL2 Assets"
    readonly property string main: ui
    readonly property string numbers: ui
    readonly property string reading: ui
    readonly property string code: mono
    function family(kind) {
        switch (kind) {
        case "title":
            return title;
        case "mono":
        case "code":
            return mono;
        case "icon":
            return icons;
        default:
            return main;
        }
    }
}
