// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// The colors of ONE desktop widget. They depend on the widget style (Look.theme) and on how bright the
// wallpaper is behind the widget: on a light wallpaper the text turns dark and the card turns light.
QtObject {
    id: p
    property real wall: 0.3         // brightness of the wallpaper behind the widget, 0 (black) .. 1 (white)
    property real k: 1              // card opacity from the editor (0 = no card)
    property bool light: false      // light wallpaper: dark text on a light card

    readonly property string theme: Look.theme
    readonly property bool glass: Look.blur && !Look.flat
    readonly property color acc: Look.accent
    readonly property real hue: acc.hslHue < 0 ? 0 : acc.hslHue
    readonly property real sat: acc.hslSaturation < 0.08 ? acc.hslSaturation : Math.max(0.42, Math.min(0.9, acc.hslSaturation))

    function decide() {
        if (!Look.adapt) { light = false; return }
        // two thresholds, so a widget on the edge of dark and light does not flicker
        if (wall > 0.58) light = true
        else if (wall < 0.47) light = false
    }
    onWallChanged: decide()
    Component.onCompleted: decide()
    readonly property bool adaptOn: Look.adapt
    onAdaptOnChanged: decide()

    // ---- text
    property color fg: light ? "#15151a" : "#F6F5F4"
    property color fg2: light ? "#33333c" : "#c8c8cc"
    property color fg3: light ? "#55555f" : "#8a8a8f"
    property color fg4: light ? "#6d6d78" : "#6a6a70"       // faint: dates of other months, hints
    Behavior on fg { ColorAnimation { duration: 400 } }
    Behavior on fg2 { ColorAnimation { duration: 400 } }
    Behavior on fg3 { ColorAnimation { duration: 400 } }
    Behavior on fg4 { ColorAnimation { duration: 400 } }

    // ---- lines, tracks, little buttons
    property color track: light ? Qt.rgba(0, 0, 0, 0.14) : Qt.rgba(1, 1, 1, 0.13)
    property color chip: light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.08)
    property color chipHover: light ? Qt.rgba(0, 0, 0, 0.16) : Qt.rgba(1, 1, 1, 0.16)
    Behavior on track { ColorAnimation { duration: 400 } }
    Behavior on chip { ColorAnimation { duration: 400 } }
    Behavior on chipHover { ColorAnimation { duration: 400 } }

    // ---- the main color: rings, the day of today, play buttons
    property color primary: theme === "google"
        ? (light ? Qt.hsla(hue, sat, 0.34, 1) : Qt.hsla(hue, sat, 0.78, 1))
        : fg
    property color inkOn: theme === "google" ? (light ? "#ffffff" : Qt.hsla(hue, Math.min(sat, 0.5), 0.12, 1))
                                                  : (light ? "#F6F5F4" : "#0d0d0d")
    property color warn: light ? "#c0392b" : "#e06c75"
    // a second, warm color for the expressive widgets (play buttons, the GPU)
    property color tertiary: light ? Qt.hsla((hue + 0.5) % 1, 0.5, 0.45, 1) : Qt.hsla((hue + 0.5) % 1, 0.55, 0.72, 1)
    property color inkTertiary: light ? "#ffffff" : Qt.hsla((hue + 0.5) % 1, 0.5, 0.14, 1)
    // the soft fill of a shaped widget (a little stronger than a card)
    property color shapeFill: theme === "outline" ? Qt.rgba(light ? 0 : 1, light ? 0 : 1, light ? 0 : 1, 0.08)
                            : (light ? Qt.hsla(hue, sat * 0.4, 0.88, Look.flat ? 1 : 0.86 * Math.max(0.3, k)) : Qt.hsla(hue, sat * 0.3, 0.17, Look.flat ? 1 : 0.8 * Math.max(0.3, k)))
    Behavior on primary { ColorAnimation { duration: 400 } }
    Behavior on inkOn { ColorAnimation { duration: 400 } }

    // ---- the card behind the widget
    readonly property real radius: theme === "google" ? 30 : (theme === "outline" ? 20 : 26)
    property color cardColor: {
        if (theme === "outline") return "transparent"
        const a = k > 0.02 ? 1 : 0
        if (theme === "google") {
            if (light) return Qt.hsla(hue, sat < 0.08 ? sat : sat * 0.6, 0.91, Look.flat ? a : (glass ? 0.78 * k : 0.92 * k))
            return Qt.hsla(hue, sat < 0.08 ? sat : sat * 0.34, 0.15, Look.flat ? a : (glass ? 0.66 * k : 0.88 * k))
        }
        if (light) return Look.flat ? Qt.rgba(0.95, 0.95, 0.96, a) : (glass ? Qt.rgba(0.97, 0.97, 0.99, 0.55 * k) : Qt.rgba(0.96, 0.96, 0.97, 0.86 * k))
        return Look.flat ? Qt.rgba(0.12, 0.12, 0.135, a) : (glass ? Qt.rgba(0.10, 0.10, 0.115, 0.46 * k) : Qt.rgba(0.17, 0.17, 0.19, 0.74 * k))
    }
    Behavior on cardColor { ColorAnimation { duration: 400 } }
    readonly property real edgeWidth: theme === "outline" ? 1.5 : (theme === "google" ? 0 : 1)
    property color edge: theme === "outline" ? Qt.rgba(light ? 0 : 1, light ? 0 : 1, light ? 0 : 1, 0.42 * (k > 0.02 ? 1 : 0.5))
                       : light ? Qt.rgba(0, 0, 0, 0.09 * k) : Qt.rgba(1, 1, 1, (glass ? 0.10 : 0.06) * k)
    Behavior on edge { ColorAnimation { duration: 400 } }
    // a soft light along the top of dark glass (the DarkShadow look only)
    readonly property bool shine: theme === "shadow" && glass && !light
}
