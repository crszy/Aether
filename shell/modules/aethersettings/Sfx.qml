pragma Singleton
// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
// Windows: DSS's "tap" pack ships in assets/sfx/tap and plays through QtMultimedia; without it the panel is silent.
// Only this panel plays them, never other apps. On/off and volume: the Panel page (Prefs.sfx, Prefs.sfxVolume).
import QtQuick
import Quickshell

Singleton {
    id: sfx
    readonly property bool enabled: Prefs.sfx
    readonly property int volume: Prefs.sfxVolume
    property var bank: null
    property var lastAt: ({})
    readonly property var gaps: ({ slide: 45, type: 30, click: 35 })
    function file(ev) { return Quickshell.shellPath("assets/sfx/tap/" + ev + ".wav") }
    function play(ev) {
        if (!Prefs.loaded || !enabled || volume <= 0)
            return;
        warm();
        if (!bank)
            return;
        const now = Date.now(), g = gaps[ev] || 0;
        if (g > 0 && lastAt[ev] && now - lastAt[ev] < g)
            return;
        lastAt[ev] = now;
        bank.play(ev);
    }
    // made when the panel opens, so the shell loads no audio until Settings is used
    function warm() {
        if (bank)
            return;
        const c = Qt.createComponent("SfxBank.qml");
        if (c.status === Component.Ready)
            bank = c.createObject(sfx, { sfx: sfx });
    }
}
