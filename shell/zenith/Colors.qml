pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// Colour tokens for the whole shell, regenerated from the wallpaper.
//
// These used to be hardcoded hex literals, so recolouring meant rewriting this
// file and restarting Quickshell -- which is exactly what zenith-theme.sh did
// (`killall quickshell`, then relaunch). That threw away every open menu and
// all shell state just to change a colour.
//
// Now matugen writes the palette to a JSON file and this singleton watches it.
// Every `Colors.*` reference in the shell is a QML binding, so when the file
// changes the whole UI -- bar, menus, dashboard -- recolours in place, with no
// restart and nothing lost.
//
// The property names below are unchanged, so no other file needed touching.

QtObject {
    id: palette

    readonly property string palettePath: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.cache/zenith/colors.json"

    // Parsed palette, or {} until the file exists. Keys match the property
    // names below and the template in themes/matugen/colors.json.
    property var scheme: ({})

    // Falls back to the built-in value whenever the generated palette is
    // missing a key -- so a first run (or a failed matugen) still renders a
    // complete, correct theme instead of black-on-black.
    function tone(name, fallback) {
        var v = scheme[name];
        return (typeof v === "string" && v.length > 0) ? v : fallback;
    }

    property FileView _paletteFile: FileView {
        path: palette.palettePath
        // Recolour the moment matugen rewrites the file.
        watchChanges: true
        printErrors: false

        onFileChanged: reload()
        onLoaded: {
            try {
                var parsed = JSON.parse(text());
                if (parsed && typeof parsed === "object") palette.scheme = parsed;
            } catch (e) {
                console.warn("zenith: colour palette is not valid JSON, keeping current theme:", e);
            }
        }
        // No palette yet: the built-in fallbacks below are already correct.
        onLoadFailed: (error) => palette.scheme = ({})
    }

    readonly property color primary: tone("primary", "#ffb68d")
    readonly property color on_primary: tone("on_primary", "#532200")
    readonly property color primary_container: tone("primary_container", "#6f3812")
    readonly property color on_primary_container: tone("on_primary_container", "#ffdbc9")
    readonly property color secondary: tone("secondary", "#e5bfaa")
    readonly property color on_secondary: tone("on_secondary", "#432b1d")
    readonly property color secondary_container: tone("secondary_container", "#5c4131")
    readonly property color on_secondary_container: tone("on_secondary_container", "#ffdbc9")
    readonly property color tertiary: tone("tertiary", "#cdc991")
    readonly property color on_tertiary: tone("on_tertiary", "#333208")
    readonly property color tertiary_container: tone("tertiary_container", "#4a481d")
    readonly property color on_tertiary_container: tone("on_tertiary_container", "#e9e5ab")
    readonly property color error: tone("error", "#ffb4ab")
    readonly property color on_error: tone("on_error", "#690005")
    readonly property color error_container: tone("error_container", "#93000a")
    readonly property color on_error_container: tone("on_error_container", "#ffdad6")
    readonly property color background: tone("background", "#1a120d")
    readonly property color on_background: tone("on_background", "#f0dfd7")
    readonly property color surface: tone("surface", "#1a120d")
    readonly property color on_surface: tone("on_surface", "#f0dfd7")
    readonly property color surface_variant: tone("surface_variant", "#52443c")
    readonly property color on_surface_variant: tone("on_surface_variant", "#d7c2b8")
    readonly property color outline: tone("outline", "#9f8d84")
    readonly property color outline_variant: tone("outline_variant", "#52443c")
    readonly property color surface_container_lowest: tone("surface_container_lowest", "#140d08")
    readonly property color surface_container_low: tone("surface_container_low", "#221a15")
    readonly property color surface_container: tone("surface_container", "#271e19")
    readonly property color surface_container_high: tone("surface_container_high", "#312823")
    readonly property color surface_container_highest: tone("surface_container_highest", "#3d332d")
    readonly property color accent: tone("accent", "#ffb68d")
}
