// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
// The interface sounds loaded once so they play at once; each is played silently when loaded so the first real
// click does not lose its start while the audio output wakes up.
import QtQuick
import QtMultimedia

QtObject {
    id: bank
    property var sfx
    readonly property real vol: sfx ? sfx.volume / 100 : 0.45
    function src(ev) {
        if (!sfx)
            return "";
        const p = String(sfx.file(ev));
        return p.indexOf("file:") === 0 ? p : "file:///" + p.replace(/^\/+/, "");
    }

    component Snd: SoundEffect {
        property bool warming: false
        volume: warming ? 0 : bank.vol
        onStatusChanged: {
            if (status === SoundEffect.Ready) {
                warming = true;
                play();
            }
        }
        onPlayingChanged: if (!playing)
            warming = false
    }
    property SoundEffect click: Snd { source: bank.src("click") }
    property SoundEffect select: Snd { source: bank.src("select") }
    property SoundEffect done: Snd { source: bank.src("done") }
    property SoundEffect switchOn: Snd { source: bank.src("switch-on") }
    property SoundEffect switchOff: Snd { source: bank.src("switch-off") }
    property SoundEffect slide: Snd { source: bank.src("slide") }
    property SoundEffect type: Snd { source: bank.src("type") }
    property SoundEffect pageNext: Snd { source: bank.src("page-next") }
    property SoundEffect pagePrev: Snd { source: bank.src("page-prev") }
    readonly property var map: ({ click: click, select: select, done: done, "switch-on": switchOn, "switch-off": switchOff,
                                  slide: slide, type: type, "page-next": pageNext, "page-prev": pagePrev })
    function play(ev) {
        const s = map[ev];
        if (!s || s.status !== SoundEffect.Ready || s.warming)
            return false;
        s.play();
        return true;
    }
}
