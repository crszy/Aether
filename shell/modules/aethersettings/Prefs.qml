pragma Singleton
// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
// Windows: everything the panel remembers (its own look, sounds) is one JSON file, ~/.config/caelestia/aether-settings.json,
// read and written with a FileView (DSS used bash + python3).
import QtQuick
import Quickshell
import Quickshell.Io
import qs.utils as CU

Singleton {
    id: prefs
    property real scroll: 1.8
    property bool shown: false
    property bool previews: false
    property string motion: "expressive"
    property real motionSpeed: 1.0
    property real textScale: 1.0
    property bool emphHeadings: false
    property bool roundText: false
    property bool switchIcons: true
    property real sliderThick: 1.0
    property bool wavy: true
    property int progressThick: 4
    property real cornerScale: 1.0
    property string accentShape: "verysunny"
    property real bounce: 1.0
    property real size: 1.0
    property bool stars: true
    property bool sfx: true
    property bool expert: false   // false: Simple (the in-use shell first, no file paths); true: every option
    property int sfxVolume: 45
    readonly property real wheel: 1.0 * scroll
    property bool loaded: false

    function clamp(v, a, b) { return Math.max(a, Math.min(b, Number(v))) }
    function setMotion(v) { motion = v; save("motion", v) }
    function setMotionSpeed(v) { motionSpeed = clamp(Math.round(v * 20) / 20, 0.5, 2); save("motionSpeed", motionSpeed) }
    function setPreviews(v) { previews = v; save("previews", v) }
    function setTextScale(v) { textScale = clamp(Math.round(v * 20) / 20, 0.85, 1.3); save("textScale", textScale) }
    function setEmphHeadings(v) { emphHeadings = v; save("emphHeadings", v) }
    function setRoundText(v) { roundText = v; save("roundText", v) }
    function setSwitchIcons(v) { switchIcons = v; save("switchIcons", v) }
    function setSliderThick(v) { sliderThick = clamp(Math.round(v * 10) / 10, 0.6, 1.6); save("sliderThick", sliderThick) }
    function setWavy(v) { wavy = v; save("wavy", v) }
    function setProgressThick(v) { progressThick = clamp(Math.round(v), 2, 8); save("progressThick", progressThick) }
    function setCornerScale(v) { cornerScale = clamp(Math.round(v * 20) / 20, 0.3, 1.5); save("cornerScale", cornerScale) }
    function setAccentShape(v) { accentShape = v; save("accentShape", v) }
    function setBounce(v) { bounce = clamp(Math.round(v * 10) / 10, 0, 2); save("bounce", bounce) }
    function setSize(v) { size = clamp(Math.round(v * 20) / 20, 0.75, 1.5); save("size", size) }
    function setScroll(v) { scroll = clamp(Math.round(v * 20) / 20, 0.3, 3); save("scroll", scroll) }
    function setStars(v) { stars = v; save("stars", v) }
    function setExpert(v) { expert = v; save("expert", v) }
    function setSfx(v) { sfx = v; save("sfx", v) }
    function setSfxVolume(v) { sfxVolume = clamp(Math.round(v), 0, 100); save("sfxVolume", sfxVolume) }

    function winW(sw, k) { return Math.min(sw - 20, Math.round(Math.max(Math.min(1000, sw - 40), Math.min(1180, Math.round(sw * 0.6))) * k)) }
    function winH(sh, k) { return Math.min(sh - 40, Math.round(Math.max(Math.min(680, sh - 60), Math.min(820, Math.round(sh * 0.74))) * k)) }

    property var doc: ({})
    function save(key, value) {
        const d = JSON.parse(JSON.stringify(doc || {}));
        d[key] = value;
        doc = d;
        saveTimer.restart();
    }
    Timer {
        id: saveTimer
        interval: 300
        onTriggered: file.setText(JSON.stringify(prefs.doc, null, 2) + "\n")
    }

    function num(j, k, a, b, def) {
        return j[k] === undefined || !isFinite(Number(j[k])) ? def : clamp(j[k], a, b);
    }
    function apply(t) {
        let j = {};
        try {
            j = JSON.parse(t) || {};
        } catch (e) {
            j = {};
        }
        doc = j;
        scroll = num(j, "scroll", 0.3, 3, 1.8);
        previews = j.previews === true;
        motion = j.motion === "standard" ? "standard" : "expressive";
        motionSpeed = num(j, "motionSpeed", 0.5, 2, 1);
        textScale = num(j, "textScale", 0.85, 1.3, 1);
        emphHeadings = j.emphHeadings === true;
        roundText = j.roundText === true;
        switchIcons = j.switchIcons !== false;
        sliderThick = num(j, "sliderThick", 0.6, 1.6, 1);
        wavy = j.wavy !== false;
        progressThick = num(j, "progressThick", 2, 8, 4);
        cornerScale = num(j, "cornerScale", 0.3, 1.5, 1);
        if (typeof j.accentShape === "string" && j.accentShape !== "")
            accentShape = j.accentShape;
        bounce = num(j, "bounce", 0, 2, 1);
        size = num(j, "size", 0.75, 1.5, 1);
        stars = j.stars !== false;
        sfx = j.sfx !== false;
        expert = j.expert === true;
        sfxVolume = num(j, "sfxVolume", 0, 100, 45);
        loaded = true;
    }

    FileView {
        id: file
        path: CU.Paths.config + "/aether-settings.json"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: prefs.apply(text())
        onLoadFailed: prefs.apply("{}")
    }
}
