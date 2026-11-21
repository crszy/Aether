// Windows (Aether): Quickshell.Networking (NetworkManager) does not exist on Windows. This stand-in lets Ryoku's
// network widgets load; Wi-Fi shows as on with no device list until Aether's Windows network backend is wired in.
pragma Singleton
import QtQuick

QtObject {
    property bool wifiEnabled: true
    property bool wifiHardwareEnabled: true
    property int backend: 0
    property bool connectivityCheckEnabled: false
    property bool canCheckConnectivity: false
    property int connectivity: 0
    readonly property QtObject devices: QtObject { property var values: [] }
    function checkConnectivity() {}
}
