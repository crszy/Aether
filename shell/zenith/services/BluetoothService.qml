pragma Singleton
import QtQuick
import Quickshell
import "../Settings"

// Windows stand-in for Zenith's BluetoothService (the original talks to BlueZ through bluetoothctl and gdbus,
// which do not exist on Windows). Same API, reporting "no Bluetooth service"; HardwareService.hasBluetooth
// is false, so Zenith leaves its Bluetooth panel out. The buttons open Windows' Bluetooth settings.
Item {
    id: service

    property var devices: []
    property int deviceCount: devices.length
    property bool powered: false
    property bool connected: false
    property bool scanning: false
    property bool serviceActive: false
    property bool isServiceEnabled: false
    property string state: "Unavailable"

    readonly property bool isPerformingAction: false
    readonly property bool isRefreshing: false
    property bool busy: false
    readonly property bool userBusy: false

    property string connectedName: ""
    property string connectedAddress: ""
    property int connectedBattery: -1
    property string connectedIcon: "bluetooth"

    function _settings() {
        Quickshell.execDetached(["explorer.exe", "ms-settings:bluetooth"]);
    }

    function refresh(full, userInitiated) {}
    function toggleStartup() { _settings(); }
    function restartService() { _settings(); }
    function updateModel(newDevices) {}
    function togglePower() { _settings(); }
    function toggleScan() { _settings(); }
    function startScan() {}
    function stopScan() {}
    function action(mode, addr) { _settings(); }
}
