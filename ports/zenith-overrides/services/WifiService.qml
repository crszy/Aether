import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as Win
import "../Settings"

pragma Singleton

// Windows version of Zenith's WifiService (the original drives nmcli through scripts/wifi_nm.py).
// Same properties, signals and functions, backed by Quickshell.Windows' Network (the native WLAN API).
// Scanning: Windows scans in the background by itself and Win.Network picks those results up; rescan()
// asks for a hardware scan, which Win.Network throttles to one per 30 s - scanning more often makes the
// adapter leave your network's channel and ping spikes to seconds.
Item {
    id: service

    property var networks: []
    property var knownNetworks: ({})

    // Station & Connection Info
    property string currentState: Win.Network.activeSsid !== "" ? "connected" : "disconnected"
    property string currentSsid: Win.Network.activeSsid
    property string ipv4Address: ""
    property string rssi: Win.Network.activeSignalQuality > 0 ? Win.Network.activeSignalQuality + "%" : ""
    property string txBitrate: ""
    property string frequency: ""
    property bool isAirplane: !Win.Network.wifiRadioOn

    // Speeds & Speed Test
    property string currentSpeed: "0.0 Mbps"
    property bool isTesting: false
    property bool isUserTyping: false
    readonly property bool isRefreshing: Win.Network.wifiScanning

    signal connectionFailed(string ssid)
    signal connectionSuccess(string ssid)

    function _level(pct) {
        return pct >= 75 ? 4 : pct >= 50 ? 3 : pct >= 25 ? 2 : pct > 0 ? 1 : 0;
    }

    // rebuilds `networks` / `knownNetworks` from Win.Network's list (wifi_nm.py's shape and order)
    function _rebuild() {
        const list = [];
        const known = {};
        const seen = {};
        const vals = Win.Network.networks.values;
        for (let i = 0; i < vals.length; i++) {
            const n = vals[i];
            if (!n.ssid || seen[n.ssid])
                continue;
            seen[n.ssid] = true;
            if (n.hasProfile)
                known[n.ssid] = true;
            list.push({
                ssid: n.ssid,
                signalPct: n.strength,
                signal: _level(n.strength),
                security: (n.security || "").toLowerCase(),
                connected: n.active,
                isKnown: n.hasProfile
            });
        }
        list.sort((a, b) => (b.connected - a.connected) || (b.isKnown - a.isKnown)
            || (b.signalPct - a.signalPct) || a.ssid.toLowerCase().localeCompare(b.ssid.toLowerCase()));
        networks = list;
        knownNetworks = known;
    }

    function refresh(fullScan) {
        _rebuild();
        _readIp();
        if (fullScan)
            Win.Network.scanWifiNetworks();
    }

    function listCached() {
        _rebuild();
    }

    function rescan() {
        Win.Network.scanWifiNetworks();
        _rebuild();
    }

    // Zenith's button says "airplane"; on Windows that is the Wi-Fi radio switch
    function toggleAirplane(block) {
        Win.Network.setWifiRadioEnabled(!!block);
    }

    function runMaxSpeedTest() {
        if (isTesting)
            return;
        isTesting = true;
        speedTestProcess.running = false;
        speedTestProcess.running = true;
    }

    property string _pendingConnectSsid: ""

    function connect(ssid, password) {
        _pendingConnectSsid = ssid;
        Win.Network.connectToNetwork(ssid, password || "");
    }

    function disconnect() {
        Win.Network.disconnectActive();
    }

    function forgetNetwork(ssid) {
        if (ssid)
            Win.Network.forgetNetwork(ssid);
    }

    function _readIp() {
        if (!ipProc.running)
            ipProc.running = true;
    }

    Connections {
        target: Win.Network.networks
        function onValuesChanged() { service._rebuild(); }
    }

    Connections {
        target: Win.Network
        function onActiveSsidChanged() { service._rebuild(); service._readIp(); }
        function onWifiConnectResult(ssid, success, reason) {
            if (success)
                service.connectionSuccess(ssid);
            else
                service.connectionFailed(ssid);
            service._pendingConnectSsid = "";
            service._rebuild();
        }
    }

    // the Wi-Fi adapter's IPv4 address, from ipconfig (no scan, no radio use)
    Process {
        id: ipProc
        command: ["ipconfig"]
        stdout: StdioCollector {
            onStreamFinished: {
                let inWifi = false;
                let ip = "";
                for (const line of text.split(/\r?\n/)) {
                    if (/^\S/.test(line))
                        inWifi = /wireless|wi-?fi|wlan/i.test(line);
                    else if (inWifi && !ip) {
                        const m = /IPv4[^:]*:\s*([\d.]+)/.exec(line);
                        if (m)
                            ip = m[1];
                    }
                }
                service.ipv4Address = ip;
            }
        }
    }

    // downloads 10 MB once, only when the speed-test button is pressed
    Process {
        id: speedTestProcess
        command: ["curl.exe", "-L", "-m", "15", "-w", "%{speed_download}", "-o", "NUL", "-s",
            "https://speed.cloudflare.com/__down?bytes=10485760"]
        stdout: StdioCollector {
            onStreamFinished: {
                const bytesPerSec = parseFloat(text.trim());
                service.currentSpeed = (!isNaN(bytesPerSec) && bytesPerSec > 0)
                    ? (bytesPerSec * 8 / 1000000).toFixed(1) + " Mbps" : "Check Connection";
                service.isTesting = false;
            }
        }
        onExited: if (service.isTesting) service.isTesting = false
    }

    Component.onCompleted: {
        Win.Network.setWifiListVisible(true); // passive: reads Windows' cached list, never scans
        Qt.callLater(refresh);
    }
}
