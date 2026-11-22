pragma Singleton

// Caelestia for Windows: Noctalia's NetworkService on Quickshell.Windows' Network (WLAN API + network list
// manager) instead of NetworkManager/nmcli and Quickshell.Networking. Same properties and functions as the
// original, so Noctalia's bar widget, Wi-Fi panel and control centre work unchanged.
import QtQuick
import Quickshell
import Quickshell.Windows as Win
import qs.noctalia.Commons
import qs.noctalia.Services.UI

Singleton {
  id: root

  readonly property var supportedSecurityTypes: [
    { key: "open", name: I18n.tr("wifi.panel.security-open") },
    { key: "wpa-psk", name: I18n.tr("wifi.panel.security-wpa") },
    { key: "wpa2-psk", name: I18n.tr("wifi.panel.security-wpa23") },
    { key: "sae", name: I18n.tr("wifi.panel.security-wpa3") }
  ]

  readonly property bool wifiAvailable: Win.Network.wifiAdapterPresent
  readonly property bool ethernetAvailable: Win.Network.ethernetConnected
  readonly property bool internetConnectivity: Win.Network.hasInternet
  readonly property string networkConnectivity: _networkConnectivity
  readonly property string _networkConnectivity: Win.Network.hasInternet ? "full" : (wifiConnected || ethernetConnected ? "limited" : "none")
  readonly property bool _wifiAvailable: wifiAvailable
  readonly property bool _ethernetAvailable: ethernetAvailable
  readonly property bool _internetConnectivity: internetConnectivity
  property string lastError: ""
  property int activeDetailsTtlMs: 10000

  // Ethernet: Windows tells us "connected or not" here
  readonly property var ethernetInterfaces: Win.Network.ethernetConnected ? [{ ifname: "Ethernet", state: "connected", connected: true, connectionName: "Ethernet" }] : []
  readonly property var activeEthernetDetails: Win.Network.ethernetConnected ? { ifname: "Ethernet", connectionName: "Ethernet" } : ({})
  readonly property bool ethernetConnected: Win.Network.ethernetConnected
  readonly property string activeEthernetIf: ethernetConnected ? "Ethernet" : ""
  property bool ethernetDetailsLoading: false
  property double activeEthernetDetailsTimestamp: 0

  // Wi-Fi
  readonly property bool wifiEnabled: Win.Network.wifiRadioOn
  readonly property var networks: {
    const map = {};
    for (const n of Win.Network.networks.values) {
      if (!n || !n.ssid)
        continue;
      const cur = map[n.ssid];
      if (cur && (cur.connected || cur.signal >= n.strength))
        continue;
      map[n.ssid] = {
        ssid: n.ssid,
        security: n.security || "--",
        signal: n.strength,
        connected: n.active,
        existing: n.hasProfile
      };
    }
    return map;
  }
  readonly property var activeWifiDetails: wifiConnected ? { ifname: "Wi-Fi", connectionName: Win.Network.activeSsid, signal: Win.Network.activeSignalQuality, security: Win.Network.activeSecurity } : ({})
  readonly property bool wifiConnected: Win.Network.activeSsid.length > 0
  readonly property string activeWifiIf: wifiConnected ? "Wi-Fi" : ""
  property bool wifiDetailsLoading: false
  property double activeWifiDetailsTimestamp: 0
  property bool wifiInit: true

  readonly property bool connecting: Win.Network.wifiConnecting
  readonly property string connectingTo: Win.Network.wifiConnectingSsid
  property string disconnectingFrom: ""
  property string forgettingNetwork: ""
  property bool scanPending: false
  readonly property bool scanningActive: Win.Network.wifiScanning
  readonly property var existingProfiles: {
    const m = {};
    for (const k in networks)
      if (networks[k].existing)
        m[k] = true;
    return m;
  }

  // airplane mode = Wi-Fi radio off here
  readonly property bool airplaneModeEnabled: false
  property bool airplaneModeToggled: false

  Connections {
    target: Win.Network
    function onWifiConnectResult(ssid, success, reason) {
      if (success) {
        root.lastError = "";
        ToastService.showNotice(I18n.tr("common.wifi"), I18n.tr("toast.wifi.connected", { "ssid": ssid }), "wifi");
      } else {
        root.lastError = reason || I18n.tr("toast.wifi.incorrect-password");
      }
    }
  }

  Component.onCompleted: Win.Network.setWifiListVisible(true)

  function setWifiEnabled(enabled) {
    Win.Network.setWifiRadioEnabled(enabled);
  }

  function setAirplaneMode(state) {
    Win.Network.setWifiRadioEnabled(!state);
  }

  function scan() {
    Win.Network.scanWifiNetworks();
  }

  function connect(ssid, password = "", isHidden = false, securityKey = "", identity = "", enterpriseConfig = {}) {
    root.lastError = "";
    Win.Network.connectToNetwork(ssid, password);
  }

  function disconnect(ssid) {
    root.disconnectingFrom = ssid;
    Win.Network.disconnectActive();
    root.disconnectingFrom = "";
  }

  function forget(ssid) {
    root.forgettingNetwork = ssid;
    Win.Network.forgetNetwork(ssid);
    root.forgettingNetwork = "";
  }

  function refreshActiveWifiDetails() {}
  function refreshActiveEthernetDetails() {}
  function updateNetworkStatus(ssid, connected) {}

  function getSignalInfo(signal, isConnected) {
    let icon = "";
    if (isConnected) {
      if (root._networkConnectivity === "limited")
        icon = "wifi-exclamation";
    }
    const label = signal >= 80 ? I18n.tr("wifi.signal.excellent") : signal >= 60 ? I18n.tr("wifi.signal.good") : signal >= 35 ? I18n.tr("wifi.signal.fair") : signal >= 15 ? I18n.tr("wifi.signal.poor") : I18n.tr("wifi.signal.weak");
    if (!icon)
      icon = signal >= 80 ? "wifi" : signal >= 60 ? "wifi-3" : signal >= 35 ? "wifi-2" : signal >= 15 ? "wifi-1" : "wifi-0";
    return { icon, label };
  }

  function isSecured(security) {
    return security && security !== "--" && security.trim() !== "" && security.toLowerCase() !== "open";
  }

  function isEnterprise(security) {
    if (!security)
      return false;
    const s = security.toUpperCase();
    return s.indexOf("802.1X") !== -1 || s.indexOf("EAP") !== -1 || s.indexOf("ENTERPRISE") !== -1;
  }

  function parseIpDetails(text) {
    return ({});
  }

  function getStatusText(showSpeed = false) {
    if (root.connecting)
      return root.connectingTo ? I18n.tr("common.connecting") + " " + root.connectingTo : I18n.tr("common.connecting");
    if (root.ethernetConnected)
      return "Ethernet";
    if (!root.wifiEnabled)
      return "";
    if (root.wifiConnected)
      return Win.Network.activeSsid;
    return "";
  }

  function getIcon(forceEthernet = false) {
    if (root.ethernetConnected || forceEthernet) {
      switch (root._networkConnectivity) {
      case "limited":
        return "ethernet-exclamation";
      case "full":
        return "ethernet";
      default:
        return "ethernet-off";
      }
    }
    if (!root.wifiEnabled)
      return "wifi-off";
    if (root.wifiConnected)
      return root.getSignalInfo(Win.Network.activeSignalQuality, true).icon;
    if (root.connecting || Object.values(root.networks).length > 0)
      return "wifi-question";
    return root.wifiAvailable ? "wifi-0" : "wifi-off";
  }
}
