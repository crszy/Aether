pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Services.UPower
import Quickshell.Windows as Win

// Windows version of Zenith's HardwareService (the original probes sysfs with scripts/detect_hardware.sh).
// Battery comes from UPower's Windows emulation (no battery on a desktop -> its widget is left out),
// Wi-Fi/Ethernet from Quickshell.Windows' Network. Backlight = a laptop panel (= a battery is present).
QtObject {
    id: service

    readonly property bool hasBattery: UPower.displayDevice ? UPower.displayDevice.isLaptopBattery : false
    property bool hasBluetooth: false   // Zenith's Bluetooth panel needs BlueZ; Windows' is not wired in
    readonly property bool hasWifi: Win.Network.wifiAdapterPresent
    readonly property bool hasEthernet: true
    readonly property bool hasBacklight: hasBattery
    property bool isVM: false
    property string virtType: "none"
    readonly property bool isPortable: hasBattery
    property bool detected: true

    readonly property bool hasNetworking: hasWifi || hasEthernet
}
