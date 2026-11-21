pragma Singleton
pragma ComponentBehavior: Bound

// Windows build of Caelestia's Nmcli service: the same public API the UI uses, backed by
// Quickshell.Windows.Network (WlanAPI) instead of the nmcli command line tool.
import QtQuick
import Quickshell
import Quickshell.Windows as Win

Singleton {
    id: root

    // ---- state the UI reads ----
    readonly property bool isConnected: Win.Network.hasInternet || Win.Network.ethernetConnected || active !== null
    readonly property bool connecting: Win.Network.wifiConnecting
    property string activeInterface: active ? "Wi-Fi" : (Win.Network.ethernetConnected ? "Ethernet" : "")
    property string activeConnection: active ? active.ssid : (Win.Network.ethernetConnected ? "Ethernet" : "")
    readonly property bool wifiEnabled: Win.Network.wifiRadioOn
    readonly property bool scanning: Win.Network.wifiScanning
    property list<AccessPoint> networks: []
    readonly property AccessPoint active: networks.find(n => n && n.active) ?? null
    readonly property list<string> savedConnections: savedConnectionSsids
    readonly property list<string> savedConnectionSsids: networks.filter(n => n && n.lastIpcObject && n.lastIpcObject.hasProfile).map(n => n.ssid)
    property var savedConnectionSecurity: ({})
    property var pendingConnection: null
    property var wirelessDeviceDetails: null
    property var ethernetDeviceDetails: null
    property string ethernetDataUsage: ""
    property string ethernetSpeed: ""
    property list<EthernetDevice> ethernetDevices: []
    readonly property EthernetDevice activeEthernet: ethernetDevices.find(d => d.connected) ?? null
    readonly property bool hasAvailableEthernet: ethernetDevices.length > 0
    property var deviceStatus: null
    property var wirelessInterfaces: Win.Network.wifiAdapterPresent ? [{ device: "Wi-Fi", type: "wifi", state: Win.Network.wifiConnecting ? "connecting" : (active ? "connected" : "disconnected"), connection: active ? active.ssid : "" }] : []
    property var ethernetInterfaces: Win.Network.ethernetConnected ? [{ device: "Ethernet", type: "ethernet", state: "connected", connection: "Ethernet" }] : []

    readonly property alias connectionCheckTimer: connectionCheckTimer
    readonly property alias immediateCheckTimer: immediateCheckTimer

    signal connectionFailed(string ssid)

    // ---- Wi-Fi list: one AccessPoint per network the Windows backend reports ----
    Instantiator {
        id: apInst
        model: Win.Network.networks
        delegate: AccessPoint {
            required property var modelData
            lastIpcObject: modelData
        }
        onObjectAdded: root.rebuildNetworks()
        onObjectRemoved: root.rebuildNetworks()
    }
    function rebuildNetworks(): void {
        const list = [];
        for (let i = 0; i < apInst.count; i++) {
            const o = apInst.objectAt(i);
            if (o && o.lastIpcObject && o.ssid && o.ssid.length > 0)
                list.push(o);
        }
        networks = list;
    }

    // ---- Ethernet: Windows only tells us "connected or not" here ----
    Component {
        id: ethComp
        EthernetDevice {}
    }
    function rebuildEthernet(): void {
        ethernetDevices = Win.Network.ethernetConnected
            ? [ethComp.createObject(root, { lastIpcObject: { interface: "Ethernet", type: "ethernet", state: "connected", connection: "Ethernet", connected: true } })]
            : [];
    }
    Connections {
        target: Win.Network
        function onEthernetConnectedChanged(): void { root.rebuildEthernet(); }
        function onWifiConnectResult(ssid: string, success: bool, reason: string): void {
            const p = root.pendingConnection;
            root.pendingConnection = null;
            if (!success)
                root.connectionFailed(ssid);
            if (p && p.callback)
                p.callback({ success: success, needsPassword: !success && !p.hadPassword && !root.hasSavedProfile(ssid),
                             output: reason, error: success ? "" : reason, exitCode: success ? 0 : 1 });
        }
    }
    Component.onCompleted: {
        rebuildEthernet();
        Win.Network.setWifiListVisible(true);   // keep the backend scanning so the list is fresh
    }

    Timer { id: connectionCheckTimer; interval: 4000 }
    Timer { id: immediateCheckTimer; property int checkCount: 0; interval: 500 }

    // ---- functions the UI calls (same names and callback shapes as the nmcli version) ----
    function rescanWifi(): void { Win.Network.scanWifiNetworks(); }
    function enableWifi(enabled: bool, callback: var): void {
        Win.Network.setWifiRadioEnabled(enabled);
        if (callback) callback({ success: true, output: "", error: "", exitCode: 0 });
    }
    function toggleWifi(callback: var): void { enableWifi(!wifiEnabled, callback); }
    function findNetwork(ssid: string): var { return networks.find(n => n.ssid === ssid) ?? null; }
    function hasSavedProfile(ssid: string): bool { const n = findNetwork(ssid); return n ? n.lastIpcObject.hasProfile : false; }
    function savedSecurityFor(ssid: string): string { const n = findNetwork(ssid); return n ? n.security : ""; }
    function securityLabel(keyMgmt: string): string { return keyMgmt && keyMgmt.length ? keyMgmt : qsTr("Open"); }
    function connectingSsid(): string { return Win.Network.wifiConnectingSsid; }

    function connectToNetwork(ssid: string, password: string, bssid: string, callback: var): void {
        pendingConnection = { ssid: ssid, bssid: bssid ?? "", callback: callback, hadPassword: !!password };
        Win.Network.connectToNetwork(ssid, password ?? "");
    }
    function connectToNetworkWithPasswordCheck(ssid: string, isSecure: bool, callback: var, bssid: string): void {
        if (isSecure && !hasSavedProfile(ssid)) {
            if (callback) callback({ success: false, needsPassword: true, output: "", error: "password required", exitCode: 1 });
            return;
        }
        connectToNetwork(ssid, "", bssid, callback);
    }
    function addHiddenNetwork(ssid: string, password: string, security: string, hidden: bool, callback: var): void {
        connectToNetwork(ssid, password, "", callback);
    }
    function disconnectFromNetwork(): void { Win.Network.disconnectActive(); }
    function forgetNetwork(ssid: string, callback: var): void {
        Win.Network.forgetNetwork(ssid);
        if (callback) callback({ success: true, output: "", error: "", exitCode: 0 });
    }
    function loadSavedConnections(callback: var): void { if (callback) callback(savedConnectionSsids); }
    function setAutoconnect(connectionName: string, enabled: bool, callback: var): void {
        if (callback) callback({ success: false, output: "", error: "not supported on Windows", exitCode: 1 });
    }
    // Ethernet and IP details: Windows Settings owns these
    function connectEthernet(connectionName: string, interfaceName: string, callback: var): void { if (callback) callback({ success: false, error: "not supported on Windows" }); }
    function disconnectEthernet(connectionName: string, callback: var): void { if (callback) callback({ success: false, error: "not supported on Windows" }); }
    function getEthernetInterfaces(callback: var): void { if (callback) callback(ethernetInterfaces); }
    function getEthernetSpeed(interfaceName: string): void {}
    function getEthernetDataUsage(interfaceName: string, callback: var): void { if (callback) callback(""); }
    function getEthernetDeviceDetails(interfaceName: string, callback: var): void { if (callback) callback(null); }
    function getWirelessDeviceDetails(interfaceName: string, callback: var): void {
        wirelessDeviceDetails = active ? { ssid: active.ssid, bssid: active.bssid, signal: active.strength, frequency: active.frequency, security: active.security } : null;
        if (callback) callback(wirelessDeviceDetails);
    }
    function getIpv(connectionName: string, family: string, callback: var): void { if (callback) callback(""); }
    function setIpv(connectionName: string, family: string, method: string, address: string, gateway: string, dns: string, callback: var): void {
        Win.Network.openWifiSettings();
        if (callback) callback({ success: false, error: "opened Windows settings" });
    }

    component AccessPoint: QtObject {
        required property var lastIpcObject
        readonly property string ssid: lastIpcObject.ssid
        readonly property string bssid: lastIpcObject.bssid
        readonly property int strength: lastIpcObject.strength
        readonly property int frequency: lastIpcObject.frequency
        readonly property bool active: lastIpcObject.active
        readonly property string security: lastIpcObject.security
        readonly property bool isSecure: security.length > 0
    }

    component EthernetDevice: QtObject {
        required property var lastIpcObject
        readonly property string iface: lastIpcObject.interface
        readonly property string type: lastIpcObject.type
        readonly property string state: lastIpcObject.state
        readonly property string connection: lastIpcObject.connection
        readonly property bool connected: lastIpcObject.connected
        readonly property string ipAddress: lastIpcObject.ipAddress ?? ""
        readonly property string gateway: lastIpcObject.gateway ?? ""
        readonly property var dns: lastIpcObject.dns ?? []
        readonly property string subnet: lastIpcObject.subnet ?? ""
        readonly property string macAddress: lastIpcObject.macAddress ?? ""
        readonly property string speed: lastIpcObject.speed ?? ""
    }
}
