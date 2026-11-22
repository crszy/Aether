import QtQuick
import Quickshell
import Quickshell.Services.UPower
import "../Settings"

pragma Singleton

// Windows version of Zenith's PowerProfileService (the original uses powerprofilesctl). Same API, backed by
// Quickshell.Services.UPower's PowerProfiles, which Caelestia for Windows maps to Windows' power modes
// (Best power efficiency / Balanced / Best performance). Battery charge limits are vendor tools on
// Windows, so the conservation card reports "not supported".
Item {
    id: service

    readonly property string currentProfile: PowerProfiles.profile === PowerProfile.PowerSaver ? "powersave"
        : PowerProfiles.profile === PowerProfile.Performance ? "performance" : "balanced"
    readonly property bool available: true

    function setProfile(profile) {
        if (profile === "powersave" || profile === "power-saver")
            PowerProfiles.profile = PowerProfile.PowerSaver;
        else if (profile === "performance" || profile === "turbo")
            PowerProfiles.profile = PowerProfile.Performance;
        else
            PowerProfiles.profile = PowerProfile.Balanced;
    }

    function update() {}

    property bool conservativeSupported: false
    property bool conservativeActive: false
    property string conservativeLabel: ""
    property string conservativeError: "Charge limits are set by your laptop maker's app on Windows"

    function refreshConservative() {}
    function toggleConservativeMode() {}
}
