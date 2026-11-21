// Mod: NightTokyo - from 20:00 to 06:00 Aether wears the TokyoRGB skin, by day your normal look.
// Switch it on: rename to NightTokyo.qml. Change the hours or the skin below.
import QtQuick
import qs.services

Timer {
    property var aether                         // Aether's helpers (see mods/README in the user folder README.txt)
    readonly property int nightFrom: 20
    readonly property int nightTo: 6
    readonly property string nightSkin: "TokyoRGB"

    function check(): void {
        const h = Time.date.getHours();
        const night = h >= nightFrom || h < nightTo;
        const want = night ? nightSkin : "";
        if (aether && aether.get("aetherSkin") !== want)
            aether.skin(want);
    }

    interval: 60000
    running: true
    repeat: true
    triggeredOnStart: true
    onTriggered: check()
    onAetherChanged: check()
}
