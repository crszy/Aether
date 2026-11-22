pragma Singleton

// "Aether style": the design of the AetherSplash (splash.cpp) as tokens every Aether surface uses - the splash,
// Aether Settings, your panels, the Aether look. Colours: the splash's own M3 dark scheme seeded from the logo
// purple (WinSettings.aetherColours "aether"), or your colour scheme ("scheme") so everything matches the wallpaper.
// Motion: three springs + the M3 emphasized curves, as plain functions of time (for Canvas drawing) and as
// QML easing curves (for Behaviors).
// NB: no property here may be called on<Capital> - QML reads that as a signal handler; hence text*/fg* names.
import QtQuick
import Quickshell
import Quickshell.Io
import qs.services

Singleton {
    id: root

    readonly property bool followScheme: WinSettings.aetherColours === "scheme"
    property bool lucky: false          // the 1-in-20 four-leaf-clover variant (the splash rolls it)

    // ---- colour tokens ----
    readonly property color surface: root.ovc("m3surface", followScheme ? Colours.palette.m3surface : lucky ? "#12140F" : "#141218")
    readonly property color textMain: root.ovc("m3onSurface", followScheme ? Colours.palette.m3onSurface : lucky ? "#E3E3DB" : "#E6E0E9")
    readonly property color textSub: root.ovc("m3onSurfaceVariant", followScheme ? Colours.palette.m3onSurfaceVariant : lucky ? "#C4C8BA" : "#CAC4D0")
    readonly property color outlineVar: root.ovc("m3outlineVariant", followScheme ? Colours.palette.m3outlineVariant : lucky ? "#44483E" : "#49454F")
    readonly property color primary: root.ovc("m3primary", followScheme ? Colours.palette.m3primary : lucky ? "#A7D389" : "#D0BCFF")
    readonly property color primaryContainer: root.ovc("m3primaryContainer", followScheme ? Colours.palette.m3primaryContainer : lucky ? "#2B4F18" : "#4F378B")
    readonly property color textOnPrimaryContainer: root.ovc("m3onPrimaryContainer", followScheme ? Colours.palette.m3onPrimaryContainer : lucky ? "#C2EFA3" : "#EADDFF")
    readonly property color tertiary: root.ovc("m3tertiary", followScheme ? Colours.palette.m3tertiary : lucky ? "#A0CFD0" : "#8AD3F0")
    readonly property color errorAccent: "#F2B8B5"
    readonly property color errorContainer: "#8C1D18"
    readonly property color textOnError: "#FFDAD6"
    readonly property color warnAccent: "#FFB951"
    readonly property color warnContainer: "#563E00"
    readonly property color textOnWarn: "#FFDEA7"
    readonly property real cornerXL: root.overrides.radius !== undefined ? Number(root.overrides.radius) : 28

    // ---- the palette Aether's own windows use (Aether Settings, your panels): the Aether scheme, or yours ----
    readonly property var basePalette: followScheme ? Colours.palette : fixedPalette
    // ...with your overrides on top (any m3* name, e.g. { "m3primary": "#ff2bd6" }): see `overrides` below
    readonly property var palette: root.ovKeys.length ? root.merged(basePalette) : basePalette

    // ---- your overrides, for everything Aether draws itself ----
    // ~/.config/caelestia/user/style.json, then the active skin's skins/<skin>/style.json on top
    // (WinSettings.aetherSkin). Keys: any m3* colour name, "radius" (corner size of Aether's windows/cards).
    readonly property string skin: UserExt.validName(WinSettings.aetherSkin) ? WinSettings.aetherSkin : ""
    property var userStyle: ({})
    property var skinStyle: ({})
    readonly property var overrides: Object.assign({}, userStyle, skin ? skinStyle : {})
    readonly property var ovKeys: Object.keys(overrides).filter(k => k.startsWith("m3"))

    // a colour token: the override when there is one, else the normal value
    function ovc(key: string, normal: color): color {
        const v = root.overrides[key];
        return v !== undefined ? Qt.lighter(String(v), 1) : normal;
    }

    readonly property var paletteKeys: ["m3primary", "m3onPrimary", "m3primaryContainer", "m3onPrimaryContainer", "m3secondary",
        "m3onSecondary", "m3secondaryContainer", "m3onSecondaryContainer", "m3tertiary", "m3onTertiary", "m3tertiaryContainer",
        "m3onTertiaryContainer", "m3error", "m3onError", "m3errorContainer", "m3onErrorContainer", "m3background", "m3surface",
        "m3surfaceDim", "m3surfaceBright", "m3surfaceContainerLowest", "m3surfaceContainerLow", "m3surfaceContainer",
        "m3surfaceContainerHigh", "m3surfaceContainerHighest", "m3onSurface", "m3surfaceVariant", "m3onSurfaceVariant",
        "m3outline", "m3outlineVariant", "m3inverseSurface", "m3inverseOnSurface", "m3inversePrimary", "m3shadow", "m3scrim"]

    function merged(base: var): var {
        const o = {};
        for (const k of root.paletteKeys)
            o[k] = root.overrides[k] !== undefined ? Qt.lighter(String(root.overrides[k]), 1) : base[k];
        return o;
    }

    function readJson(view: var): var {
        try {
            const t = view.text().trim();
            return t ? JSON.parse(t) : {};
        } catch (e) {
            console.warn("[aether] style:", view.path, e);
            return {};
        }
    }

    FileView {
        id: userStyleFile

        path: `${UserExt.dir}/style.json`
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.userStyle = root.readJson(userStyleFile)
        onLoadFailed: root.userStyle = {}
    }

    FileView {
        id: skinStyleFile

        path: root.skin ? `${UserExt.dir}/skins/${root.skin}/style.json` : ""
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.skinStyle = root.readJson(skinStyleFile)
        onLoadFailed: root.skinStyle = {}
    }

    QtObject {
        id: fixedPalette

        readonly property color m3primary: "#d0bcff"
        readonly property color m3onPrimary: "#381e72"
        readonly property color m3primaryContainer: "#4f378b"
        readonly property color m3onPrimaryContainer: "#eaddff"
        readonly property color m3secondary: "#ccc2dc"
        readonly property color m3onSecondary: "#332d41"
        readonly property color m3secondaryContainer: "#4a4458"
        readonly property color m3onSecondaryContainer: "#e8def8"
        readonly property color m3tertiary: "#8ad3f0"
        readonly property color m3onTertiary: "#003546"
        readonly property color m3tertiaryContainer: "#004d63"
        readonly property color m3onTertiaryContainer: "#bde9ff"
        readonly property color m3error: "#f2b8b5"
        readonly property color m3onError: "#601410"
        readonly property color m3errorContainer: "#8c1d18"
        readonly property color m3onErrorContainer: "#f9dedc"
        readonly property color m3background: "#141218"
        readonly property color m3surface: "#141218"
        readonly property color m3surfaceDim: "#141218"
        readonly property color m3surfaceBright: "#3b383e"
        readonly property color m3surfaceContainerLowest: "#0f0d13"
        readonly property color m3surfaceContainerLow: "#1d1b20"
        readonly property color m3surfaceContainer: "#211f26"
        readonly property color m3surfaceContainerHigh: "#2b2930"
        readonly property color m3surfaceContainerHighest: "#36343b"
        readonly property color m3onSurface: "#e6e0e9"
        readonly property color m3surfaceVariant: "#49454f"
        readonly property color m3onSurfaceVariant: "#cac4d0"
        readonly property color m3outline: "#938f99"
        readonly property color m3outlineVariant: "#49454f"
        readonly property color m3inverseSurface: "#e6e0e9"
        readonly property color m3inverseOnSurface: "#322f35"
        readonly property color m3inversePrimary: "#6750a4"
        readonly property color m3shadow: "#000000"
        readonly property color m3scrim: "#000000"
    }

    // ---- motion (t in seconds) ----
    function clamp01(x: real): real {
        return x < 0 ? 0 : x > 1 ? 1 : x;
    }

    // cubic bezier easing y(x) for control points (x1,y1) (x2,y2)
    function bez(x1: real, y1: real, x2: real, y2: real, x: real): real {
        x = clamp01(x);
        let t = x;
        for (let i = 0; i < 8; i++) {
            const c = 3 * x1 * t * (1 - t) * (1 - t) + 3 * x2 * t * t * (1 - t) + t * t * t - x;
            const d = 3 * x1 * (1 - t) * (1 - t) + 6 * (x2 - x1) * t * (1 - t) + 3 * (1 - x2) * t * t;
            if (Math.abs(d) < 1e-5)
                break;
            t = clamp01(t - c / d);
        }
        return 3 * y1 * t * (1 - t) * (1 - t) + 3 * y2 * t * t * (1 - t) + t * t * t;
    }

    function emphDec(x: real): real {   // enter
        return bez(0.05, 0.7, 0.1, 1, x);
    }

    function emphAcc(x: real): real {   // exit
        return bez(0.3, 0, 0.8, 0.15, x);
    }

    // damped spring step response: stiffness k, damping ratio z
    function spring(t: real, k: real, z: real): real {
        if (t <= 0)
            return 0;
        const w0 = Math.sqrt(k);
        if (z >= 1)
            return 1 - (1 + w0 * t) * Math.exp(-w0 * t);
        const wd = w0 * Math.sqrt(1 - z * z);
        return 1 - Math.exp(-z * w0 * t) * (Math.cos(wd * t) + (z * w0 / wd) * Math.sin(wd * t));
    }

    function spatial(t: real): real {    // size / position morphs
        return spring(t, 170, 0.9);
    }

    function effects(t: real): real {    // opacity / colour
        return spring(t, 600, 1);
    }

    function expressive(t: real): real { // playful overshoot
        return spring(t, 300, 0.6);
    }

    // the same feel for QML Behaviors: SpringAnimation { spring: AetherStyle.springSpatial.s; damping: ... }
    readonly property var springSpatial: ({ s: 3.2, d: 0.32 })
    readonly property var springExpressive: ({ s: 4.2, d: 0.2 })
    readonly property var emphasizedDecel: [0.05, 0.7, 0.1, 1, 1, 1]
    readonly property var emphasizedAccel: [0.3, 0, 0.8, 0.15, 1, 1]

    // a CSS colour string for Canvas drawing, with alpha 0..1
    function css(c: color, a: real): string {
        return `rgba(${Math.round(c.r * 255)},${Math.round(c.g * 255)},${Math.round(c.b * 255)},${Math.max(0, Math.min(1, a))})`;
    }

    function mix(a: color, b: color, t: real): color {
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1);
    }
}
