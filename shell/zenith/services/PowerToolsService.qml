pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Services.Pipewire
import Quickshell.Windows as Win
import Caelestia.State
import qs.services as Cae
import "../Settings"

// Windows version of Zenith's PowerToolsService (the original uses gammastep, wpctl, notify-send, grim,
// hyprpicker, and polls wpctl/pidof every 3 s). Same API:
//   night light -> Windows' gamma night light (Quickshell.Windows NightLight, 3800 K like Zenith's)
//   Do Not Disturb -> Caelestia's DND, so it silences every shell's popups alike
//   mic -> the default input through Pipewire's Windows emulation (no polling)
//   screenshot -> Caelestia's area picker
QtObject {
    id: root

    property bool nightLight: false
    readonly property bool dnd: PersistentState.doNotDisturb
    readonly property bool micMuted: Pipewire.defaultAudioSource?.audio?.muted ?? false

    readonly property bool caffeineActive: CaffeineService.active

    function toggleCaffeine() {
        CaffeineService.toggle();
    }

    function toggleDnd() {
        PersistentState.doNotDisturb = !PersistentState.doNotDisturb;
    }

    function toggleNightLight() {
        nightLight = !nightLight;
        if (nightLight)
            Win.NightLight.enable(3800);
        else
            Win.NightLight.disable();
    }

    function toggleMicMute() {
        const s = Pipewire.defaultAudioSource;
        if (s?.audio)
            s.audio.muted = !s.audio.muted;
    }

    function refreshMicState() {}

    function triggerScreenshot(mode) {
        MenuService.closeAll();
        delayTimer.callback = () => Cae.WinSettings.ipc("picker", mode === "clip" ? "openClip" : "open");
        delayTimer.restart();
    }

    // Windows has no system colour picker: PowerToys' (Win+Shift+C) is the usual one
    function triggerColorPicker() {
        MenuService.closeAll();
    }

    property PwObjectTracker _track: PwObjectTracker {
        objects: [Pipewire.defaultAudioSource]
    }

    property Timer delayTimer: Timer {
        id: delayTimer
        interval: 250
        property var callback: null
        onTriggered: if (callback) callback()
    }
}
