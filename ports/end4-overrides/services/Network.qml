pragma Singleton
pragma ComponentBehavior: Bound

import Quickshell
import QtQuick
import Quickshell.Windows as Win
import qs.end4.services.network

// Windows version of end-4's Network service (which runs nmcli). Same properties and functions, backed by
// Quickshell for Windows' Network (the native WLAN API). No background scanning: Windows scans by itself and
// the list follows its results; rescanWifi() (the refresh button / opening the list) asks for one hardware scan,
// which Win.Network allows at most every 30 s - frequent scans make Wi-Fi pings spike.
Singleton {
    id: root

    property bool wifi: Win.Network.wifiAdapterPresent
    property bool ethernet: Win.Network.ethernetConnected
    property bool wifiEnabled: Win.Network.wifiRadioOn
    property bool wifiScanning: Win.Network.wifiScanning
    property bool wifiConnecting: Win.Network.wifiConnecting
    property WifiAccessPoint wifiConnectTarget
    property var wifiNetworks: []
    readonly property WifiAccessPoint active: wifiNetworks.find(n => n.active) ?? null
    readonly property list<var> friendlyWifiNetworks: [...wifiNetworks].sort((a, b) => {
        if (a.active && !b.active)
            return -1;
        if (!a.active && b.active)
            return 1;
        return b.strength - a.strength;
    })
    property string wifiStatus: !Win.Network.wifiRadioOn ? "disabled" : Win.Network.wifiConnecting ? "connecting" : Win.Network.activeSsid !== "" ? "connected" : "disconnected"

    property string networkName: ethernet ? "Ethernet" : Win.Network.activeSsid
    property int networkStrength: Win.Network.activeSignalQuality
    property string materialSymbol: root.ethernet
        ? "lan"
        : (root.wifiEnabled && root.wifiStatus === "connected")
            ? (
                networkStrength > 83 ? "signal_wifi_4_bar" :
                networkStrength > 67 ? "network_wifi" :
                networkStrength > 50 ? "network_wifi_3_bar" :
                networkStrength > 33 ? "network_wifi_2_bar" :
                networkStrength > 17 ? "network_wifi_1_bar" :
                "signal_wifi_0_bar"
            )
            : (root.wifiStatus === "connecting")
                ? "signal_wifi_statusbar_not_connected"
                : (root.wifiStatus === "disconnected")
                    ? "wifi_find"
                    : (root.wifiStatus === "disabled")
                        ? "signal_wifi_off"
                        : "signal_wifi_bad"

    function enableWifi(enabled = true): void {
        Win.Network.setWifiRadioEnabled(enabled);
    }

    function toggleWifi(): void {
        enableWifi(!wifiEnabled);
    }

    function rescanWifi(): void {
        Win.Network.setWifiListVisible(true);
        Win.Network.scanWifiNetworks();
        root.update();
    }

    // a secured network Windows has no profile for needs its password first (the list then shows the field)
    function connectToWifiNetwork(accessPoint: WifiAccessPoint): void {
        root.wifiConnectTarget = accessPoint;
        if (accessPoint.isSecure && !accessPoint.lastIpcObject.hasProfile) {
            accessPoint.askingPassword = true;
            return;
        }
        accessPoint.askingPassword = false;
        Win.Network.connectToNetwork(accessPoint.ssid, "");
    }

    function disconnectWifiNetwork(): void {
        Win.Network.disconnectActive();
    }

    function openPublicWifiPortal() {
        Qt.openUrlExternally("http://www.msftconnecttest.com/redirect");
    }

    function changePassword(network: WifiAccessPoint, password: string, username = ""): void {
        network.askingPassword = false;
        root.wifiConnectTarget = network;
        Win.Network.connectToNetwork(network.ssid, password);
    }

    Connections {
        target: Win.Network
        function onWifiConnectResult(ssid, success, reason) {
            if (!success && root.wifiConnectTarget && root.wifiConnectTarget.ssid === ssid && root.wifiConnectTarget.isSecure)
                root.wifiConnectTarget.askingPassword = true;
            root.update();
        }
        function onActiveSsidChanged() {
            root.update();
        }
    }

    Connections {
        target: Win.Network.networks
        function onValuesChanged() {
            root.update();
        }
    }

    // rebuilds wifiNetworks from Win.Network's list, keeping the objects of networks still there
    function update() {
        const seen = new Map();
        for (const n of (Win.Network.networks.values ?? [])) {
            if (!n.ssid)
                continue;
            const next = {
                active: !!n.active,
                strength: Number(n.strength) || 0,
                frequency: 0,
                ssid: n.ssid,
                bssid: "",
                security: /^(open|none)?$/i.test(String(n.security || "")) ? "" : String(n.security),
                hasProfile: !!n.hasProfile
            };
            const existing = seen.get(n.ssid);
            if (!existing || (next.active && !existing.active) || (!existing.active && next.strength > existing.strength))
                seen.set(n.ssid, next);
        }
        const list = root.wifiNetworks.slice();
        for (let i = list.length - 1; i >= 0; i--) {
            if (!seen.has(list[i].ssid)) {
                list[i].destroy();
                list.splice(i, 1);
            }
        }
        for (const [ssid, data] of seen) {
            const match = list.find(n => n.ssid === ssid);
            if (match)
                match.lastIpcObject = data;
            else
                list.push(apComp.createObject(root, { lastIpcObject: data }));
        }
        root.wifiNetworks = list;
    }

    Component.onCompleted: update()

    Component {
        id: apComp
        WifiAccessPoint {}
    }
}
