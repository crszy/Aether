// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick
import QtMultimedia

// The interface sounds loaded once, so they play at once (needs QtMultimedia; Sfx.qml works without it).
// A sound that is still loading (right after you pick another pack) is not played here: play() says
// false and Sfx.qml plays the file another way, so no click is ever silent.
// Every sound is played once without volume as soon as it is loaded: the first real click then
// does not lose its start while the audio output wakes up.
QtObject {
    id: bank
    property var sfx
    readonly property real vol: sfx ? sfx.volume / 100 : 0.45
    function src(ev) { return sfx ? "file:///" + sfx.file(ev) : "" }

    component Snd: SoundEffect {
        property bool warming: false
        volume: warming ? 0 : bank.vol
        onStatusChanged: {
            if (status === SoundEffect.Ready) { warming = true; play() }
        }
        onPlayingChanged: if (!playing) warming = false
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
    property SoundEffect addSnd: Snd { source: bank.src("add") }
    property SoundEffect removeSnd: Snd { source: bank.src("remove") }
    readonly property var map: ({ click: click, select: select, done: done, "switch-on": switchOn, "switch-off": switchOff,
                                  slide: slide, type: type, "page-next": pageNext, "page-prev": pagePrev, add: addSnd, remove: removeSnd })
    // false: not ready here (the caller plays it another way)
    function play(ev) {
        const s = map[ev]
        if (!s || s.status !== SoundEffect.Ready || s.warming) return false
        s.play()
        return true
    }
}
