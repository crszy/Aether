pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Windows as Win

// Windows network backend for DMS's NetworkService (the original one, DMSNetworkService, talks to DMS's Go
// backend and NetworkManager). Backed by Quickshell for Windows' Network (the native WLAN API).
// Scanning: Windows scans in the background by itself; the list is only read while a DMS panel shows it
// (addRef/removeRef), and a hardware scan happens only when the user asks - Win.Network throttles those to
// one per 30 s, since frequent scans make Wi-Fi pings spike.
Singleton {
    id: root

    readonly property string backend: "windows"
    readonly property bool networkAvailable: true
    readonly property bool wifiAvailable: Win.Network.wifiAdapterPresent
    readonly property bool wifiEnabled: Win.Network.wifiRadioOn
    readonly property bool wifiConnected: Win.Network.activeSsid !== ""
    readonly property bool ethernetConnected: Win.Network.ethernetConnected
    readonly property string currentWifiSSID: Win.Network.activeSsid
    readonly property int wifiSignalStrength: Win.Network.activeSignalQuality
    readonly property string networkStatus: ethernetConnected ? "ethernet" : (wifiConnected ? "wifi" : "disconnected")
    readonly property string primaryConnection: ethernetConnected ? "Ethernet" : currentWifiSSID
    readonly property bool isConnecting: Win.Network.wifiConnecting
    readonly property string connectingSSID: Win.Network.wifiConnectingSsid
    readonly property bool isScanning: Win.Network.wifiScanning
    readonly property bool autoScan: false
    property bool wifiToggling: false
    readonly property bool cellularToggling: false
    readonly property bool cellularEnabled: false
    readonly property bool cellularHardwareEnabled: false
    readonly property var cellularDevices: []
    readonly property var cellularConnections: []
    readonly property var ethernetDevices: ethernetConnected ? [{ name: "Ethernet", interface: "Ethernet", connected: true, state: "connected" }] : []
    readonly property var wiredConnections: []
    readonly property var wifiDevices: wifiAvailable ? [{ name: "Wi-Fi", interface: "Wi-Fi", connected: wifiConnected }] : []
    readonly property string wifiInterface: wifiAvailable ? "Wi-Fi" : ""
    readonly property string ethernetInterface: ethernetConnected ? "Ethernet" : ""
    readonly property string userPreference: "auto"
    property string connectionError: ""
    property var wifiNetworks: []
    property var savedWifiNetworks: []
    property var savedConnections: []
    property var ssidToConnectionName: ({})
    readonly property int savedWifiStateApiVersion: 26

    readonly property var wifiSignalIcon: {
        if (isConnecting)
            return "wifi";
        if (!wifiConnected)
            return "wifi_off";
        if (wifiSignalStrength >= 50)
            return "wifi";
        if (wifiSignalStrength >= 25)
            return "wifi_2_bar";
        return "wifi_1_bar";
    }

    signal networksUpdated
    signal connectionChanged
    signal credentialsNeeded(string token, string ssid, string setting, var fields, var hints, string reason, string connType, string connName, string vpnService, var fieldsInfo)

    property int _refs: 0

    function addRef() {
        if (++_refs === 1)
            Win.Network.setWifiListVisible(true);
        _rebuild();
    }

    function removeRef() {
        _refs = Math.max(0, _refs - 1);
        if (_refs === 0)
            Win.Network.setWifiListVisible(false);
    }

    function _rebuild() {
        const seen = {};
        const list = [];
        for (const n of (Win.Network.networks.values ?? [])) {
            if (!n.ssid || seen[n.ssid])
                continue;
            seen[n.ssid] = true;
            const security = String(n.security || "");
            list.push({
                ssid: n.ssid,
                signal: n.strength,
                secured: security !== "" && !/^(open|none)$/i.test(security),
                enterprise: /enterprise|802\.1x|eap/i.test(security),
                saved: !!n.hasProfile,
                connected: !!n.active,
                autoconnect: !!n.hasProfile
            });
        }
        list.sort((a, b) => (b.connected - a.connected) || (b.signal - a.signal) || a.ssid.toLowerCase().localeCompare(b.ssid.toLowerCase()));
        wifiNetworks = list;
        const saved = list.filter(n => n.saved).map(n => Object.assign({}, n, { outOfRange: false }));
        savedWifiNetworks = saved;
        savedConnections = saved;
        const names = {};
        for (const n of saved)
            names[n.ssid] = n.ssid;
        ssidToConnectionName = names;
        networksUpdated();
    }

    Connections {
        target: Win.Network.networks
        function onValuesChanged() {
            if (root._refs > 0)
                root._rebuild();
        }
    }

    Connections {
        target: Win.Network
        function onActiveSsidChanged() {
            root.connectionChanged();
        }
        function onEthernetConnectedChanged() {
            root.connectionChanged();
        }
        function onWifiRadioOnChanged() {
            root.wifiToggling = false;
        }
        function onWifiConnectResult(ssid, success, reason) {
            root.connectionError = success ? "" : (reason || "Could not connect");
            root.connectionChanged();
            if (root._refs > 0)
                root._rebuild();
        }
    }

    function getState() {
        _rebuild();
    }

    // only on request; Win.Network throttles real scans
    function scanWifi() {
        Win.Network.scanWifiNetworks();
    }

    function scanWifiNetworks() {
        Win.Network.scanWifiNetworks();
    }

    function startAutoScan() {}
    function stopAutoScan() {}

    function refreshSavedWifiNetworks() {
        _rebuild();
    }

    function connectToWifi(ssid, password = "", username = "", anonymousIdentity = "", domainSuffixMatch = "", hidden = false, eapMethod = "", phase2Auth = "") {
        connectionError = "";
        Win.Network.connectToNetwork(ssid, password || "");
    }

    function disconnectWifi() {
        Win.Network.disconnectActive();
    }

    function disconnectAllActive() {
        Win.Network.disconnectActive();
    }

    function forgetWifiNetwork(ssid) {
        Win.Network.forgetNetwork(ssid);
    }

    function toggleWifiRadio() {
        wifiToggling = true;
        Win.Network.setWifiRadioEnabled(!Win.Network.wifiRadioOn);
    }

    function enableWifiDevice() {
        Win.Network.setWifiRadioEnabled(true);
    }

    function toggleNetworkConnection(type) {
        if (type === "wifi")
            wifiConnected ? disconnectWifi() : toggleWifiRadio();
        else
            Win.Network.openWifiSettings();
    }

    function setNetworkPreference(preference) {}
    function disconnectEthernetDevice(deviceName) {
        Quickshell.execDetached(["explorer.exe", "ms-settings:network-ethernet"]);
    }
    function setWifiAutoconnect(ssid, autoconnect) {}
}
