import QtQuick
import Quickshell
import Quickshell.Services.UPower
import "../Settings"

pragma Singleton

// Windows version of Zenith's BatteryService (the original reads /sys/class/power_supply through python and
// udevadm). Same properties, from Quickshell.Services.UPower, which Caelestia for Windows emulates on top of
// Windows' power status. A desktop has no battery: percentage stays -1 and HardwareService.hasBattery is
// false, so Zenith leaves its battery widget out.
Item {
    id: service

    readonly property var _dev: UPower.displayDevice
    readonly property bool _present: _dev ? _dev.isLaptopBattery : false

    readonly property int percentage: _present ? Math.round(_dev.percentage * 100) : -1
    readonly property string status: {
        if (!_present)
            return "unknown";
        switch (_dev.state) {
        case UPowerDeviceState.Charging:
        case UPowerDeviceState.PendingCharge:
            return "charging";
        case UPowerDeviceState.FullyCharged:
            return "full";
        case UPowerDeviceState.Discharging:
        case UPowerDeviceState.PendingDischarge:
            return "discharging";
        default:
            return "unknown";
        }
    }
    readonly property bool acOnline: !UPower.onBattery

    property int cycleCount: 0
    property real voltage: 0.0
    readonly property real energyRate: _present ? Math.abs(_dev.changeRate || 0) : 0

    property bool isFullyCharged: (status === "full" || (status === "charging" && percentage >= 100))
    property bool isConservative: false

    property string batPath: ""
    property string acPath: ""
    property string lastStatus: ""
    property bool lastAcState: false
    property int lastThreshold: 100
    property int updatesReceived: 2
    readonly property string timeRemaining: {
        if (!_present)
            return "N/A";
        if (status === "charging")
            return formatTime(_dev.timeToFull);
        if (status === "discharging")
            return formatTime(_dev.timeToEmpty);
        return "N/A";
    }
    readonly property real energyNow: _present ? (_dev.energy || 0) : 0
    readonly property real health: _present ? (_dev.healthPercentage || 0) : 0
    property real temp: 0

    function update() {}

    function getIconName(p, s) {
        const step = p >= 100 ? 100 : Math.floor(Math.max(0, p) / 10) * 10;
        if (s === "charging" || s === "full")
            return step >= 100 ? "battery-level-100-charged-symbolic" : `battery-level-${step}-charging-symbolic`;
        return `battery-level-${step}-symbolic`;
    }

    function sendNotify(title, msg, urgency) {}

    function formatTime(seconds) {
        if (seconds <= 0 || isNaN(seconds) || seconds === Infinity)
            return "N/A";
        const h = Math.floor(seconds / 3600);
        const m = Math.floor((seconds % 3600) / 60);
        return h + "h " + m + "m";
    }
}
