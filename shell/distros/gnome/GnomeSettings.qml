import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Bluetooth
import Quickshell.Windows as Win
import qs.services as Cae
import qs.utils as CaeU
import qs.modules.aethersettings as AS
import Caelestia.State as CaeState

// GNOME Settings (Debian 13 / GNOME 48 layout): a sidebar of panels on the left (search button, title, main menu),
// the panel on the right. Everything here changes the real thing on this PC (Wi-Fi, sound devices, displays,
// power plan, ...); rows Windows keeps to itself open the matching Windows settings page instead.
FloatingWindow {
    id: win

    property string page: "wifi"
    readonly property var aboutStats: page === "about" ? Win.FsUtils.systemStats() : ({})
    signal dismissed

    title: "Settings"
    implicitWidth: 980
    implicitHeight: 640
    color: "#242424"
    onClosed: {
        if (Bluetooth.defaultAdapter && Bluetooth.defaultAdapter.discovering)
            Bluetooth.defaultAdapter.discovering = false;
        dismissed();
    }

    // GNOME 48's sidebar, in its groups ("-" = the line between groups)
    readonly property var pages: [
        ["wifi", "Wi-Fi", "wifi"],
        ["network", "Network", "lan"],
        ["bluetooth", "Bluetooth", "bluetooth"],
        ["-"],
        ["displays", "Displays", "desktop_windows"],
        ["sound", "Sound", "volume_up"],
        ["power", "Power", "battery_full"],
        ["multitasking", "Multitasking", "view_carousel"],
        ["appearance", "Appearance", "palette"],
        ["-"],
        ["apps", "Apps", "apps"],
        ["notifications", "Notifications", "notifications"],
        ["search", "Search", "search"],
        ["accounts", "Online Accounts", "account_circle"],
        ["sharing", "Sharing", "share"],
        ["wellbeing", "Digital Wellbeing", "hourglass"],
        ["-"],
        ["mouse", "Mouse & Touchpad", "mouse"],
        ["keyboard", "Keyboard", "keyboard"],
        ["color", "Color", "colorize"],
        ["printers", "Printers", "print"],
        ["-"],
        ["accessibility", "Accessibility", "accessibility_new"],
        ["privacy", "Privacy & Security", "back_hand"],
        ["system", "System", "settings_suggest"]
    ]
    // System's own pages (GNOME 46+: they open from System)
    readonly property var systemPages: [
        ["region", "Region & Language", "language"],
        ["datetime", "Date & Time", "schedule"],
        ["users", "Users", "group"],
        ["remote", "Remote Desktop", "screen_share"],
        ["ssh", "Secure Shell", "terminal"],
        ["about", "About", "info"],
        ["updates", "Software Updates", "update"]
    ]
    readonly property string section: systemPages.some(p => p[0] === page) ? "system" : page

    // sidebar search (GNOME: the results are the matching panels, A to Z)
    property bool searching: false
    property string sideQuery: ""
    readonly property var sideList: {
        if (!searching || !sideQuery)
            return pages;
        const q = sideQuery.toLowerCase();
        return pages.concat(systemPages).filter(p => p[0] !== "-" && p[1].toLowerCase().includes(q)).sort((a, b) => a[1].localeCompare(b[1]));
    }

    property var input: ({})
    property string appQuery: ""
    property var printers: []
    property string powerScheme: ""
    property string proxy: ""
    property int dispIdx: 0
    property bool aboutApp: false

    onPageChanged: {
        if (page === "mouse" || page === "keyboard")
            input = typeof Win.FsUtils.inputSettings === "function" ? Win.FsUtils.inputSettings() : {};
        if (page === "printers")
            printerProc.running = true;
        if (page === "power") {
            Win.IdlePower.refresh();
            schemeProc.running = true;
        }
        if (page === "network")
            proxyProc.running = true;
        if (page === "displays")
            Win.Displays.refresh();
        if (Bluetooth.defaultAdapter && Bluetooth.defaultAdapter.enabled)
            Bluetooth.defaultAdapter.discovering = page === "bluetooth";
    }
    Component.onCompleted: pageChanged()

    function setInput(name: string, value: int): void {
        if (typeof Win.FsUtils.setInputSetting === "function" && Win.FsUtils.setInputSetting(name, value))
            input = Win.FsUtils.inputSettings();
    }
    function openWin(uri: string): void {
        Quickshell.execDetached(["explorer.exe", uri.includes(":") ? uri : "ms-settings:" + uri]);
    }
    function setScheme(alias: string): void {
        Quickshell.execDetached(["powercfg", "/setactive", alias]);
        schemeTimer.restart();
    }
    function minutesLabel(m: int): string {
        return m === 0 ? "Never" : m < 60 ? `${m} minutes` : m % 60 === 0 ? (m === 60 ? "1 hour" : `${m / 60} hours`) : `${Math.floor(m / 60)} h ${m % 60} min`;
    }

    Binding {   // the input source list is only polled while Keyboard is open
        target: Win.Keyboard
        property: "active"
        value: win.page === "keyboard"
    }
    Process {
        id: printerProc
        command: ["powershell.exe", "-NoProfile", "-Command", "Get-Printer | ForEach-Object { $_.Name + '|' + $_.PrinterStatus + '|' + ($_.Default -as [string]) }"]
        stdout: StdioCollector {
            onStreamFinished: win.printers = text.split("\n").map(l => l.trim()).filter(l => l).map(l => l.split("|"))
        }
    }
    Process {
        id: schemeProc
        command: ["powercfg", "/getactivescheme"]
        stdout: StdioCollector {
            onStreamFinished: win.powerScheme = (text.match(/[0-9a-f]{8}-[0-9a-f-]{27}/i) ?? [""])[0].toLowerCase()
        }
    }
    Timer {
        id: schemeTimer
        interval: 600
        onTriggered: schemeProc.running = true
    }
    Process {
        id: proxyProc
        command: ["reg", "query", "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"]
        stdout: StdioCollector {
            onStreamFinished: win.proxy = /AutoConfigURL\s+REG_SZ\s+\S/.test(text) ? "Automatic" : /ProxyEnable\s+REG_DWORD\s+0x1\b/.test(text) ? "Manual" : "Off"
        }
    }
    readonly property var accents: [["blue", "#3584e4"], ["teal", "#2190a4"], ["green", "#3a944a"], ["yellow", "#c88800"],
        ["orange", "#ed5b00"], ["red", "#e62d42"], ["pink", "#d56199"], ["purple", "#9141ac"], ["slate", "#6f8396"]]

    // the selected display (Displays panel)
    readonly property var disp: {
        const all = Win.Displays.displays.filter(d => d.active);
        return all.length ? all[Math.min(dispIdx, all.length - 1)] : null;
    }
    readonly property var dispSizes: {
        if (!disp)
            return [];
        const seen = {};
        return (disp.modes ?? []).filter(m => { const k = `${m.width}x${m.height}`; if (seen[k]) return false; seen[k] = true; return true; })
            .sort((a, b) => b.width * b.height - a.width * a.height)
            .map(m => [`${m.width} × ${m.height}` + (m.width / m.height > 1.7 && m.width / m.height < 1.8 ? " (16:9)" : m.width / m.height > 2.3 ? " (21:9)" : ""), `${m.width}x${m.height}`]);
    }
    readonly property var dispRates: {
        if (!disp)
            return [];
        const m = (disp.modes ?? []).find(m => m.width === disp.resolutionWidth && m.height === disp.resolutionHeight);
        return (m?.rates ?? []).slice().sort((a, b) => b - a).map(r => [`${r} Hz`, r]);
    }

    Item {
        id: rootItem
        anchors.fill: parent

        // ---- a dropdown menu (resolution, refresh rate, ...) over everything; a click outside closes it
        property var menuItems: []
        property var menuCurrent
        property var menuCallback: null
        property point menuPos
        function openMenu(from: Item, items: var, current: var, cb: var): void {
            const p = from.mapToItem(rootItem, 0, from.height + 4);
            const h = Math.min(items.length * 36 + 12, 360);
            menuPos = Qt.point(Math.min(p.x, width - 220), p.y + h > height ? Math.max(8, p.y - from.height - 8 - h) : p.y);
            menuItems = items;
            menuCurrent = current;
            menuCallback = cb;
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // ---- sidebar
            Rectangle {
                Layout.fillHeight: true
                implicitWidth: 270
                color: "#2e2e32"

                // header bar: search, title, main menu
                Item {
                    id: sideHead
                    width: parent.width
                    height: 48
                    IconButton {
                        id: searchBtn
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        icon: "search"
                        active: win.searching
                        onClicked: {
                            win.searching = !win.searching;
                            if (win.searching) searchInput.forceActiveFocus(); else { searchInput.text = ""; }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "Settings"
                        color: Adw.text
                        font.family: Adw.font
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                    IconButton {
                        id: menuBtn
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        icon: "menu"
                        active: rootItem.menuItems.length > 0 && rootItem.menuCurrent === "main"
                        onClicked: rootItem.openMenu(menuBtn, [["Keyboard Shortcuts", "shortcuts"], ["About Settings", "about"]], "main", v => {
                            if (v === "shortcuts") win.page = "keyboard"; else win.aboutApp = true;
                        })
                    }
                }
                Rectangle {
                    id: searchBox
                    anchors.top: sideHead.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 8
                    anchors.topMargin: 0
                    height: win.searching ? 36 : 0
                    visible: win.searching
                    radius: 8
                    color: Qt.rgba(1, 1, 1, 0.08)
                    Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: "search"; font.family: Adw.icons; font.pixelSize: 17; color: Adw.dim }
                    TextInput {
                        id: searchInput
                        anchors.fill: parent
                        anchors.leftMargin: 34
                        anchors.rightMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        color: Adw.text
                        font.family: Adw.font
                        font.pixelSize: 14
                        clip: true
                        onTextChanged: win.sideQuery = text
                        Keys.onEscapePressed: { text = ""; win.searching = false; }
                        Keys.onReturnPressed: if (win.sideList.length) win.page = win.sideList[0][0]
                        Text { visible: !parent.text; anchors.verticalCenter: parent.verticalCenter; text: "Search settings"; color: Adw.dim; font: parent.font }
                    }
                }

                Flickable {
                    anchors.top: searchBox.visible ? searchBox.bottom : sideHead.bottom
                    anchors.topMargin: searchBox.visible ? 8 : 0
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    contentHeight: sideCol.implicitHeight + 16
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: sideCol
                        x: 8
                        width: parent.width - 16
                        spacing: 2
                        Repeater {
                            model: win.sideList
                            Rectangle {
                                required property var modelData
                                readonly property bool sep: modelData[0] === "-"
                                width: sideCol.width
                                height: sep ? 9 : 36
                                radius: 8
                                color: sep ? "transparent" : (win.searching && win.sideQuery ? win.page : win.section) === modelData[0] ? Qt.rgba(1, 1, 1, 0.1) : sideArea.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                                Rectangle { visible: parent.sep; anchors.centerIn: parent; width: parent.width - 16; height: 1; color: Adw.border }
                                Row {
                                    visible: !parent.sep
                                    anchors.left: parent.left
                                    anchors.leftMargin: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 12
                                    Text { text: modelData[2] ?? ""; font.family: Adw.icons; font.pixelSize: 18; color: Adw.text; anchors.verticalCenter: parent.verticalCenter }
                                    Text { text: modelData[1] ?? ""; font.family: Adw.font; font.pixelSize: 14; color: Adw.text; anchors.verticalCenter: parent.verticalCenter }
                                }
                                MouseArea { id: sideArea; anchors.fill: parent; enabled: !parent.sep; hoverEnabled: true; onClicked: win.page = modelData[0] }
                            }
                        }
                        // GNOME's empty search state
                        Column {
                            visible: win.searching && win.sideQuery.length > 0 && win.sideList.length === 0
                            width: sideCol.width
                            topPadding: 60
                            spacing: 8
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "search"; font.family: Adw.icons; font.pixelSize: 64; color: Adw.dim }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "No Results Found"; color: Adw.text; font.family: Adw.font; font.pixelSize: 18; font.weight: Font.Bold }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Try a different search."; color: Adw.dim; font.family: Adw.font; font.pixelSize: 13 }
                        }
                    }
                }
            }

            // ---- the panel
            Flickable {
                id: panel
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: body.implicitHeight + 60
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Connections {
                    target: win
                    function onPageChanged(): void { panel.contentY = 0; }
                }

                ColumnLayout {
                    id: body
                    width: Math.min(640, parent.width - 60)
                    x: (parent.width - width) / 2
                    y: 20
                    spacing: 18

                    Row {
                        spacing: 10
                        Text {   // back to System (or Sharing) from one of its pages
                            visible: win.section === "system" && win.page !== "system"
                            anchors.verticalCenter: parent.verticalCenter
                            text: "arrow_back"
                            font.family: Adw.icons
                            font.pixelSize: 22
                            color: Adw.text
                            MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: win.page = "system" }
                        }
                        Text {
                            text: win.pages.concat(win.systemPages).find(p => p[0] === win.page)?.[1] ?? ""
                            color: Adw.text
                            font.family: Adw.font
                            font.pixelSize: 20
                            font.weight: Font.Bold
                        }
                    }

                    // ================= Wi-Fi
                    Page_ {
                        name: "wifi"
                        Group {
                            Row_ {
                                label: "Wi-Fi"
                                GSwitch { checked: Cae.Nmcli.wifiEnabled; onToggled: Cae.Nmcli.toggleWifi(null) }
                            }
                            Row_ { label: "Saved Networks"; sub: "Networks this PC connects to by itself"; external: true; onClicked: win.openWin("network-wifisettings") }
                            Row_ { label: "Connect to Hidden Network…"; external: true; onClicked: win.openWin("network-wifi") }
                        }
                        Column {
                            visible: !Cae.Nmcli.wifiEnabled
                            Layout.fillWidth: true
                            topPadding: 30
                            spacing: 6
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "signal_wifi_off"; font.family: Adw.icons; font.pixelSize: 64; color: Adw.dim }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Wi-Fi Off"; color: Adw.text; font.family: Adw.font; font.pixelSize: 18; font.weight: Font.Bold }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Turn on Wi-Fi to connect to a network"; color: Adw.dim; font.family: Adw.font; font.pixelSize: 13 }
                        }
                        Head_ { visible: Cae.Nmcli.wifiEnabled; text: "Visible Networks" }
                        Group {
                            visible: Cae.Nmcli.wifiEnabled
                            Row_ { visible: Cae.Nmcli.networks.length === 0; label: "Searching for networks…" }
                            Repeater {
                                model: [...Cae.Nmcli.networks].filter(n => n && n.ssid).sort((a, b) => (b.active - a.active) || (b.strength - a.strength)).slice(0, 14)
                                Row_ {
                                    required property var modelData
                                    label: modelData.ssid
                                    sub: modelData.active ? "Connected" : wifiPass.visible ? "Enter the password and press Enter" : ""
                                    Text {
                                        text: modelData.strength > 66 ? "signal_wifi_4_bar" : modelData.strength > 33 ? "network_wifi_2_bar" : "network_wifi_1_bar"
                                        font.family: Adw.icons
                                        font.pixelSize: 18
                                        color: Adw.text
                                    }
                                    Text { visible: modelData.isSecure; text: "lock"; font.family: Adw.icons; font.pixelSize: 16; color: Adw.dim }
                                    Rectangle {
                                        id: wifiPass
                                        visible: false
                                        implicitWidth: 180
                                        implicitHeight: 32
                                        radius: 8
                                        color: Qt.rgba(1, 1, 1, 0.08)
                                        TextInput {
                                            id: passInput
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            echoMode: TextInput.Password
                                            color: Adw.text
                                            font.pixelSize: 14
                                            onAccepted: { Cae.Nmcli.connectToNetwork(modelData.ssid, text, modelData.bssid, null); wifiPass.visible = false; text = ""; }
                                        }
                                    }
                                    onClicked: {
                                        if (modelData.active) return;
                                        Cae.Nmcli.connectToNetworkWithPasswordCheck(modelData.ssid, modelData.isSecure, r => {
                                            if (r && r.needsPassword) { wifiPass.visible = true; passInput.forceActiveFocus(); }
                                        }, modelData.bssid);
                                    }
                                }
                            }
                        }
                    }

                    // ================= Network
                    Page_ {
                        name: "network"
                        Head_ { text: "Wired" }
                        Group {
                            Row_ {
                                label: Cae.Nmcli.activeEthernet ? "Connected" : "Cable unplugged"
                                sub: Cae.Nmcli.activeEthernet ? "Ethernet" : ""
                                Text { text: "lan"; font.family: Adw.icons; font.pixelSize: 18; color: Cae.Nmcli.activeEthernet ? Adw.text : Adw.dim }
                                external: true
                                onClicked: win.openWin("network-ethernet")
                            }
                        }
                        Head_ { text: "VPN" }
                        Group {
                            Row_ {
                                visible: Cae.VPN.providers.length > 0
                                label: Cae.VPN.providerName || "VPN"
                                sub: Cae.VPN.connecting ? "Connecting…" : Cae.VPN.disconnecting ? "Disconnecting…" : Cae.VPN.connected ? "Connected" : "Off"
                                GSwitch { checked: Cae.VPN.connected; onToggled: Cae.VPN.toggle() }
                            }
                            Row_ {
                                visible: Cae.VPN.providers.length === 0
                                label: "Not set up"
                                sub: "Add a WireGuard, Tailscale, NetBird or WARP connection in Aether's settings"
                            }
                        }
                        Group {
                            Row_ { label: "Network Proxy"; sub: win.proxy || "…"; external: true; onClicked: win.openWin("network-proxy") }
                        }
                    }

                    // ================= Bluetooth
                    Page_ {
                        id: btPage
                        name: "bluetooth"
                        readonly property var adapter: Bluetooth.defaultAdapter
                        readonly property bool on: adapter?.enabled ?? false
                        readonly property var devices: Bluetooth.devices.values.filter(d => d.name || d.deviceName)
                        Group {
                            Row_ {
                                label: "Bluetooth"
                                sub: !btPage.adapter ? "No Bluetooth adapter found" : btPage.on ? `Visible as “${Quickshell.env("COMPUTERNAME") || "this PC"}”` : ""
                                GSwitch {
                                    visible: !!Bluetooth.defaultAdapter
                                    checked: Bluetooth.defaultAdapter?.enabled ?? false
                                    onToggled: if (Bluetooth.defaultAdapter) Bluetooth.defaultAdapter.enabled = !Bluetooth.defaultAdapter.enabled
                                }
                            }
                        }
                        Head_ { visible: btPage.on && btPage.devices.some(d => d.paired); text: "My Devices" }
                        Group {
                            visible: btPage.on && btPage.devices.some(d => d.paired)
                            Repeater {
                                model: btPage.devices.filter(d => d.paired)
                                Row_ {
                                    required property var modelData
                                    label: modelData.name || modelData.deviceName
                                    sub: modelData.connected ? (modelData.batteryAvailable ? `Connected · battery ${Math.round(modelData.battery * 100)}%` : "Connected") : "Disconnected"
                                    onClicked: modelData.connected ? modelData.disconnect() : modelData.connect()
                                }
                            }
                        }
                        Head_ { visible: btPage.on; text: "Nearby Devices" }
                        Group {
                            visible: btPage.on
                            Row_ { visible: !btPage.devices.some(d => !d.paired); label: "Searching for devices…" }
                            Repeater {
                                model: btPage.devices.filter(d => !d.paired)
                                Row_ {
                                    required property var modelData
                                    label: modelData.name || modelData.deviceName
                                    sub: modelData.pairing ? "Pairing…" : "Not Set Up"
                                    onClicked: modelData.pair()
                                }
                            }
                        }
                    }

                    // ================= Displays
                    Page_ {
                        name: "displays"
                        // the changes undo themselves unless kept, like GNOME's "Keep Changes?" dialog
                        Rectangle {
                            visible: Win.Displays.pendingConfirmation
                            Layout.fillWidth: true
                            implicitHeight: 64
                            radius: 12
                            color: Qt.alpha(Adw.accent, 0.25)
                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                Text { text: "Keep Changes?"; color: Adw.text; font.family: Adw.font; font.pixelSize: 15; font.weight: Font.Bold }
                                Text { text: `Settings changes will revert in ${Win.Displays.confirmSecondsLeft} seconds`; color: Adw.dim; font.family: Adw.font; font.pixelSize: 12 }
                            }
                            Row {
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8
                                Pill { text: "Revert Settings"; onClicked: Win.Displays.revertChanges() }
                                Pill { text: "Keep Changes"; suggested: true; onClicked: Win.Displays.keepChanges() }
                            }
                        }
                        Text {
                            visible: Win.Displays.lastError.length > 0
                            Layout.fillWidth: true
                            text: Win.Displays.lastError
                            color: "#ff7b63"
                            wrapMode: Text.Wrap
                            font.family: Adw.font
                            font.pixelSize: 13
                        }
                        // more than one display: pick which one the rows below change
                        Row {
                            visible: Win.Displays.displays.filter(d => d.active).length > 1
                            spacing: 8
                            Repeater {
                                model: Win.Displays.displays.filter(d => d.active)
                                Pill {
                                    required property var modelData
                                    required property int index
                                    text: `${index + 1}  ${modelData.name}`
                                    suggested: win.dispIdx === index
                                    onClicked: win.dispIdx = index
                                }
                            }
                        }
                        Group {
                            visible: !!win.disp
                            Row_ {
                                visible: Win.Displays.displays.filter(d => d.active).length > 1
                                label: "Primary Display"
                                sub: "The top bar and dock show here"
                                GSwitch { checked: win.disp?.primary ?? false; onToggled: if (win.disp && !win.disp.primary) Win.Displays.setPrimary(win.disp.id) }
                            }
                            Row_ {
                                label: "Orientation"
                                Drop_ {
                                    options: [["Landscape", 0], ["Portrait", 1], ["Landscape (Flipped)", 2], ["Portrait (Flipped)", 3]]
                                    current: win.disp?.rotation ?? 0
                                    onPicked: v => Win.Displays.setRotation(win.disp.id, v)
                                }
                            }
                            Row_ {
                                label: "Resolution"
                                Drop_ {
                                    options: win.dispSizes
                                    current: win.disp ? `${win.disp.resolutionWidth}x${win.disp.resolutionHeight}` : ""
                                    onPicked: v => {
                                        const [w, h] = v.split("x").map(Number);
                                        const m = win.disp.modes.find(m => m.width === w && m.height === h);
                                        const rates = m?.rates ?? [60];
                                        const rate = rates.includes(win.disp.refreshRate) ? win.disp.refreshRate : Math.max(...rates);
                                        Win.Displays.setMode(win.disp.id, w, h, rate);
                                    }
                                }
                            }
                            Row_ {
                                label: "Refresh Rate"
                                Drop_ {
                                    options: win.dispRates
                                    current: win.disp?.refreshRate ?? 0
                                    onPicked: v => Win.Displays.setMode(win.disp.id, win.disp.resolutionWidth, win.disp.resolutionHeight, v)
                                }
                            }
                            Row_ {
                                visible: win.disp?.scaleSupported ?? false
                                label: "Scale"
                                Drop_ {
                                    options: (win.disp?.scales ?? []).map(s => [s === win.disp.scaleRecommended ? `${s} % (recommended)` : `${s} %`, s])
                                    current: win.disp?.scale ?? 100
                                    onPicked: v => Win.Displays.setScale(win.disp.id, v)
                                }
                            }
                            Row_ {
                                visible: win.disp?.hdrSupported ?? false
                                label: "HDR"
                                sub: "Brighter highlights in HDR videos and games"
                                GSwitch { checked: win.disp?.hdrEnabled ?? false; onToggled: Win.Displays.setHdrEnabled(win.disp.id, !win.disp.hdrEnabled) }
                            }
                        }
                        Group {
                            Repeater {
                                model: Cae.Brightness.monitors
                                Row_ {
                                    required property var modelData
                                    label: `Brightness · ${modelData.modelData.name}`
                                    sub: modelData.available ? "" : "This display's brightness is set on the display itself"
                                    Slider_ {
                                        visible: modelData.available
                                        implicitWidth: 220
                                        value: modelData.brightness
                                        onMoved: v => modelData.setBrightness(v)
                                    }
                                }
                            }
                        }
                        Head_ { text: "Night Light" }
                        Group {
                            Row_ {
                                label: "Night Light"
                                sub: "Makes the screen warmer, to help you sleep"
                                GSwitch { checked: Cae.WinSettings.distroNightLight; onToggled: Cae.WinSettings.distroNightLight = !Cae.WinSettings.distroNightLight }
                            }
                            Row_ {
                                label: "Color Temperature"
                                sub: `${Cae.WinSettings.distroNightTemp} K`
                                Text { text: "Less Warm"; color: Adw.dim; font.family: Adw.font; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                                Slider_ {
                                    implicitWidth: 200
                                    value: (6000 - Cae.WinSettings.distroNightTemp) / 3300
                                    onMoved: v => Cae.WinSettings.distroNightTemp = Math.round((6000 - v * 3300) / 100) * 100
                                }
                                Text { text: "More Warm"; color: Adw.dim; font.family: Adw.font; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                            }
                        }
                    }

                    // ================= Sound
                    Page_ {
                        name: "sound"
                        Head_ { text: "Output" }
                        Group {
                            Row_ {
                                label: "Volume"
                                GIconBtn { icon: Cae.Audio.muted ? "volume_off" : "volume_up"; onClicked: if (Cae.Audio.sink?.audio) Cae.Audio.sink.audio.muted = !Cae.Audio.muted }
                                Slider_ { implicitWidth: 260; value: Cae.Audio.volume; onMoved: v => Cae.Audio.setVolume(v) }
                            }
                            Repeater {
                                model: Cae.Audio.sinks
                                Row_ {
                                    required property var modelData
                                    label: modelData.description || modelData.name
                                    Text { visible: Cae.Audio.sink === modelData; text: "check"; font.family: Adw.icons; font.pixelSize: 20; color: Adw.accent }
                                    onClicked: Cae.Audio.setAudioSink(modelData)
                                }
                            }
                        }
                        Head_ { text: "Input" }
                        Group {
                            Row_ {
                                label: "Volume"
                                GIconBtn { icon: Cae.Audio.sourceMuted ? "mic_off" : "mic"; onClicked: if (Cae.Audio.source?.audio) Cae.Audio.source.audio.muted = !Cae.Audio.sourceMuted }
                                Slider_ { implicitWidth: 260; value: Cae.Audio.sourceVolume; onMoved: v => Cae.Audio.setSourceVolume(v) }
                            }
                            Repeater {
                                model: Cae.Audio.sources
                                Row_ {
                                    required property var modelData
                                    label: modelData.description || modelData.name
                                    Text { visible: Cae.Audio.source === modelData; text: "check"; font.family: Adw.icons; font.pixelSize: 20; color: Adw.accent }
                                    onClicked: Cae.Audio.setAudioSource(modelData)
                                }
                            }
                        }
                        Head_ { text: "Volume Levels" }
                        Group {
                            Row_ { visible: Cae.Audio.streams.length === 0; label: "No apps are playing sound" }
                            Repeater {
                                model: Cae.Audio.streams
                                Row_ {
                                    required property var modelData
                                    label: Cae.Audio.getStreamName(modelData)
                                    GIconBtn {
                                        icon: modelData.audio?.muted ? "volume_off" : "volume_up"
                                        onClicked: Cae.Audio.setStreamMuted(modelData, !(modelData.audio?.muted ?? false))
                                    }
                                    Slider_ { implicitWidth: 220; value: modelData.audio?.volume ?? 0; onMoved: v => Cae.Audio.setStreamVolume(modelData, v) }
                                }
                            }
                        }
                    }

                    // ================= Power
                    Page_ {
                        name: "power"
                        Head_ { text: "Power Mode" }
                        Group {
                            Repeater {
                                model: [
                                    ["Performance", "High performance, more power", "SCHEME_MIN", "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c", "speed"],
                                    ["Balanced", "Standard performance and power usage", "SCHEME_BALANCED", "381b4222-f694-41f0-9685-ff5bb260df2e", "balance"],
                                    ["Power Saver", "Reduced performance and power usage", "SCHEME_MAX", "a1841308-3541-4fab-bc81-f71556f20b4a", "eco"]
                                ]
                                Row_ {
                                    required property var modelData
                                    label: modelData[0]
                                    sub: modelData[1]
                                    Radio { checked: win.powerScheme === modelData[3] }
                                    onClicked: win.setScheme(modelData[2])
                                }
                            }
                            Row_ {
                                visible: win.powerScheme !== "" && !["8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c", "381b4222-f694-41f0-9685-ff5bb260df2e", "a1841308-3541-4fab-bc81-f71556f20b4a"].includes(win.powerScheme)
                                label: "A custom Windows power plan is in use"
                                external: true
                                onClicked: win.openWin("powersleep")
                            }
                        }
                        Head_ { text: "Power Saving Options" }
                        Group {
                            Row_ {
                                label: "Screen Blank"
                                sub: "Turns the screen off after a period of inactivity"
                                Drop_ {
                                    options: [1, 2, 3, 4, 5, 8, 10, 12, 15, 30, 0].map(m => [win.minutesLabel(m), m])
                                    current: Win.IdlePower.screenOffAcMinutes
                                    onPicked: v => { Win.IdlePower.screenOffAcMinutes = v; if (Win.IdlePower.hasBattery) Win.IdlePower.screenOffDcMinutes = v; }
                                }
                            }
                            Row_ {
                                label: "Automatic Suspend"
                                sub: "Pauses the computer when it isn't being used"
                                Drop_ {
                                    options: [0, 15, 20, 25, 30, 45, 60, 80, 90, 100, 120].map(m => [m === 0 ? "Off" : win.minutesLabel(m), m])
                                    current: Win.IdlePower.sleepAcMinutes
                                    onPicked: v => { Win.IdlePower.sleepAcMinutes = v; if (Win.IdlePower.hasBattery) Win.IdlePower.sleepDcMinutes = v; }
                                }
                            }
                        }
                        Head_ { visible: Win.IdlePower.hasBattery; text: "General" }
                        Group {
                            visible: Win.IdlePower.hasBattery
                            Row_ {
                                label: "Show Battery Percentage"
                                sub: "In the top bar"
                                GSwitch { checked: Cae.WinSettings.distroBatteryPercent; onToggled: Cae.WinSettings.distroBatteryPercent = !Cae.WinSettings.distroBatteryPercent }
                            }
                        }
                        Head_ { text: "Aether" }
                        Group {
                            Row_ {
                                label: "Keep Awake"
                                sub: "The screen doesn't turn off or lock by itself"
                                GSwitch { checked: CaeState.RuntimeState.inhibitIdle; onToggled: CaeState.RuntimeState.inhibitIdle = !CaeState.RuntimeState.inhibitIdle }
                            }
                            Row_ {
                                label: "Game Mode"
                                sub: "Effects and background work pause while you play"
                                GSwitch { checked: CaeState.RuntimeState.gameMode; onToggled: CaeState.RuntimeState.gameMode = !CaeState.RuntimeState.gameMode }
                            }
                            Row_ {
                                label: "Automatic Game Mode"
                                sub: "On by itself while a fullscreen game has focus"
                                GSwitch { checked: Cae.WinSettings.autoGameMode; onToggled: Cae.WinSettings.autoGameMode = !Cae.WinSettings.autoGameMode }
                            }
                        }
                    }

                    // ================= Multitasking
                    Page_ {
                        name: "multitasking"
                        Head_ { text: "General" }
                        Group {
                            Row_ {
                                label: "Hot Corner"
                                sub: "Touch the top-left corner to open the Activities overview"
                                GSwitch { checked: Cae.WinSettings.distroHotCorner; onToggled: Cae.WinSettings.distroHotCorner = !Cae.WinSettings.distroHotCorner }
                            }
                            Row_ {
                                label: "Window Tiling"
                                sub: "AetherWM arranges windows in columns, like niri"
                                // on/off = AetherWM running or not (it used to flip hyprlandWm, which left AetherWM running
                                // bare, without hyprland.conf / config.kdl - no binds, no animations)
                                GSwitch {
                                    checked: !!Cae.WinSettings.komorebi
                                    onToggled: Cae.WinSettings.komorebi = Cae.WinSettings.komorebi ? "" : (Quickshell.env("LOCALAPPDATA") + "/AetherShell/aetherwm/bin").replace(/\\/g, "/")
                                }
                            }
                        }
                        Head_ { text: "Workspaces" }
                        Group {
                            Row_ { label: "Dynamic workspaces"; sub: "Workspaces are added and removed as you use them"; Radio { checked: true } }
                        }
                        Head_ { text: "Multi-Monitor" }
                        Group {
                            Row_ { label: "Workspaces on all displays"; sub: "Each display has its own workspaces"; Radio { checked: true } }
                        }
                    }

                    // ================= Appearance
                    Page_ {
                        name: "appearance"
                        Head_ { text: "Style" }
                        Row {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 24
                            Repeater {
                                model: [["Default", false], ["Dark", true]]
                                Column {
                                    required property var modelData
                                    spacing: 8
                                    opacity: modelData[1] ? 1 : 0.45
                                    Rectangle {
                                        width: 180
                                        height: 112
                                        radius: 12
                                        color: modelData[1] ? "#1d1d1d" : "#f6f5f4"
                                        border.width: modelData[1] ? 3 : 1
                                        border.color: modelData[1] ? Adw.accent : Adw.border
                                        Rectangle { x: 14; y: 14; width: 110; height: 70; radius: 8; color: modelData[1] ? "#363636" : "#ffffff" }
                                        Rectangle { x: 54; y: 30; width: 110; height: 70; radius: 8; color: modelData[1] ? "#454545" : "#ebebeb" }
                                    }
                                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData[0]; color: Adw.text; font.family: Adw.font; font.pixelSize: 14 }
                                }
                            }
                        }
                        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "The Debian look is dark only for now"; color: Adw.dim; font.family: Adw.font; font.pixelSize: 12 }
                        Group {
                            Row_ {
                                label: "Accent Color"
                                Row {
                                    spacing: 8
                                    Repeater {
                                        model: win.accents
                                        Rectangle {
                                            required property var modelData
                                            width: 26
                                            height: 26
                                            radius: 13
                                            color: modelData[1]
                                            border.width: Adw.accent.toString() === Qt.color(modelData[1]).toString() ? 3 : 0
                                            border.color: "#ffffff"
                                            MouseArea { anchors.fill: parent; onClicked: { Adw.accent = modelData[1]; Cae.WinSettings.distroAccent = modelData[1]; } }
                                        }
                                    }
                                }
                            }
                            Row_ {
                                label: "Liquid Glass"
                                sub: "Top bar and dock become clear glass over the background, like iOS 26"
                                GSwitch { checked: Adw.glass; onToggled: Cae.WinSettings.glass = Adw.glass ? "off" : "liquid" }
                            }
                        }
                        Head_ { text: "Background" }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 12
                            Repeater {
                                model: [""].concat(win.page === "appearance" ? Win.FsUtils.findFiles([String(CaeU.Paths.wallsdir)], ["*.jpg", "*.jpeg", "*.png", "*.webp", "*.svg"], 1, 2).slice(0, 40) : [])
                                Rectangle {
                                    required property var modelData
                                    readonly property bool current: (Cae.WinSettings.distroWallpaper || "") === modelData
                                    width: 150
                                    height: 96
                                    radius: 10
                                    color: "#1d1d1d"
                                    border.width: current ? 3 : 0
                                    border.color: Adw.accent
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        anchors.margins: parent.current ? 3 : 0
                                        source: modelData ? "file:///" + modelData : Quickshell.shellPath("distros/debian/ceratopsian-1920x1080.svg")
                                        sourceSize: Qt.size(300, 192)
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: Cae.WinSettings.distroWallpaper = modelData }
                                }
                            }
                        }
                    }

                    // ================= Apps
                    Page_ {
                        name: "apps"
                        Group {
                            Row_ { label: "Default Apps"; sub: "Web browser, mail, music, video, photos"; external: true; onClicked: win.openWin("defaultapps") }
                            Row_ { label: "Startup Apps"; external: true; onClicked: win.openWin("startupapps") }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 38
                            radius: 10
                            color: Qt.rgba(1, 1, 1, 0.08)
                            TextInput {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                verticalAlignment: Text.AlignVCenter
                                color: Adw.text
                                font.family: Adw.font
                                font.pixelSize: 14
                                onTextChanged: win.appQuery = text
                                Text { visible: !parent.text; anchors.verticalCenter: parent.verticalCenter; text: "Search apps"; color: Adw.dim; font: parent.font }
                            }
                        }
                        Group {
                            Repeater {
                                model: win.page === "apps" ? [...DesktopEntries.applications.values].filter(e => !e.noDisplay && (!win.appQuery || e.name.toLowerCase().includes(win.appQuery.toLowerCase()))).sort((a, b) => a.name.localeCompare(b.name)).slice(0, 60) : []
                                Row_ {
                                    required property var modelData
                                    label: modelData.name
                                    sub: modelData.genericName || modelData.comment || ""
                                    Text { text: "open_in_new"; font.family: Adw.icons; font.pixelSize: 18; color: Adw.dim }
                                    onClicked: modelData.execute()
                                }
                            }
                        }
                    }

                    // ================= Notifications
                    Page_ {
                        name: "notifications"
                        Group {
                            Row_ {
                                label: "Do Not Disturb"
                                GSwitch { checked: CaeState.PersistentState.doNotDisturb; onToggled: CaeState.PersistentState.doNotDisturb = !CaeState.PersistentState.doNotDisturb }
                            }
                            Row_ {
                                label: "Lock Screen Notifications"
                                sub: "Show notifications on the lock screen"
                                GSwitch { checked: Cae.WinSettings.distroLockNotifs; onToggled: Cae.WinSettings.distroLockNotifs = !Cae.WinSettings.distroLockNotifs }
                            }
                        }
                        Head_ { text: "App Notifications" }
                        Group {
                            id: notifApps
                            readonly property var apps: {
                                const counts = {};
                                for (const n of Cae.Notifs.list)
                                    counts[n.appName || "Other"] = (counts[n.appName || "Other"] ?? 0) + 1;
                                return Object.keys(counts).sort((a, b) => a.localeCompare(b)).map(k => [k, counts[k]]);
                            }
                            Row_ { visible: notifApps.apps.length === 0; label: "No apps have sent notifications yet" }
                            Repeater {
                                model: notifApps.apps
                                Row_ {
                                    required property var modelData
                                    label: modelData[0]
                                    sub: modelData[1] === 1 ? "1 notification" : `${modelData[1]} notifications`
                                }
                            }
                            Row_ { label: "Turn apps on or off"; sub: "Windows decides which apps may notify"; external: true; onClicked: Win.NotificationSettings.openSettings() }
                        }
                    }

                    // ================= Search
                    Page_ {
                        name: "search"
                        Head_ { text: "Search Results" }
                        Group {
                            Row_ { label: "Apps"; sub: "Find apps in the Activities overview by name or what they do"; GSwitch { checked: true; opacity: 0.5 } }
                            Row_ {
                                label: "Settings"
                                sub: "Show Settings panels (Wi-Fi, Sound, …) in search results"
                                GSwitch { checked: Cae.WinSettings.distroSearchSettings; onToggled: Cae.WinSettings.distroSearchSettings = !Cae.WinSettings.distroSearchSettings }
                            }
                        }
                    }

                    // ================= Online Accounts
                    Page_ {
                        name: "accounts"
                        Group {
                            Row_ { label: "No online accounts"; sub: "Aether doesn't keep your accounts - your apps sign in themselves" }
                            Row_ { label: "Email & accounts"; sub: "The accounts Windows knows about"; external: true; onClicked: win.openWin("emailandaccounts") }
                        }
                    }

                    // ================= Sharing
                    Page_ {
                        name: "sharing"
                        Group {
                            Row_ { label: "Device Name"; sub: Quickshell.env("COMPUTERNAME") || ""; external: true; onClicked: win.openWin("about") }
                        }
                        Group {
                            Row_ { label: "Remote Desktop"; chevron: true; onClicked: win.page = "remote" }
                            Row_ { label: "Secure Shell"; chevron: true; onClicked: win.page = "ssh" }
                            Row_ { label: "Screen Sharing"; sub: "Share your screen from the app you call in (Discord, browsers)" }
                        }
                    }

                    // ================= Digital Wellbeing
                    Page_ {
                        name: "wellbeing"
                        Head_ { text: "Screen Time" }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 230
                            radius: 12
                            color: "#3a3a3a"
                            id: wbChart
                            readonly property var week: { GnomeWellbeing.days; return GnomeWellbeing.week(); }
                            readonly property int most: Math.max(60, ...week.map(d => d[1]))
                            Column {
                                x: 18
                                y: 14
                                Text { text: "Today"; color: Adw.dim; font.family: Adw.font; font.pixelSize: 12 }
                                Text { text: GnomeWellbeing.format(GnomeWellbeing.todayMinutes); color: Adw.text; font.family: Adw.font; font.pixelSize: 22; font.weight: Font.Bold }
                            }
                            Column {
                                anchors.right: parent.right
                                anchors.rightMargin: 18
                                y: 14
                                Text { anchors.right: parent.right; text: "Daily average"; color: Adw.dim; font.family: Adw.font; font.pixelSize: 12 }
                                Text {
                                    anchors.right: parent.right
                                    readonly property var used: wbChart.week.filter(d => d[1] > 0)
                                    text: GnomeWellbeing.format(used.length ? Math.round(used.reduce((s, d) => s + d[1], 0) / used.length) : 0)
                                    color: Adw.text
                                    font.family: Adw.font
                                    font.pixelSize: 16
                                }
                            }
                            Row {
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 12
                                anchors.horizontalCenter: parent.horizontalCenter
                                spacing: (parent.width - 36 - 7 * 44) / 6
                                Repeater {
                                    model: wbChart.week
                                    Column {
                                        required property var modelData
                                        required property int index
                                        spacing: 4
                                        Item {
                                            width: 44
                                            height: 120
                                            Rectangle {
                                                anchors.bottom: parent.bottom
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                width: 28
                                                radius: 6
                                                height: Math.max(4, modelData[1] / wbChart.most * 120)
                                                color: index === 6 ? Adw.accent : Qt.alpha(Adw.accent, 0.45)
                                            }
                                        }
                                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData[0]; color: index === 6 ? Adw.text : Adw.dim; font.family: Adw.font; font.pixelSize: 12 }
                                    }
                                }
                            }
                        }
                        Text { Layout.fillWidth: true; text: "Counted while the Debian look is on; a minute counts when you used the mouse or keyboard."; wrapMode: Text.Wrap; color: Adw.dim; font.family: Adw.font; font.pixelSize: 12 }
                        Head_ { text: "Screen Limits" }
                        Group {
                            Row_ {
                                label: "Daily Limit"
                                sub: "A notification when today's screen time reaches it"
                                Drop_ {
                                    options: [0, 60, 120, 180, 240, 300, 360, 480].map(m => [m === 0 ? "Off" : win.minutesLabel(m), m])
                                    current: Cae.WinSettings.distroScreenLimit
                                    onPicked: v => Cae.WinSettings.distroScreenLimit = v
                                }
                            }
                        }
                        Head_ { text: "Break Reminders" }
                        Group {
                            Row_ {
                                label: "Break Reminders"
                                sub: "Reminds you to look away and move"
                                GSwitch { checked: Cae.WinSettings.distroBreaks; onToggled: Cae.WinSettings.distroBreaks = !Cae.WinSettings.distroBreaks }
                            }
                            Row_ {
                                label: "Break Schedule"
                                sub: "Screen time before a break; a 5-minute pause resets it"
                                Drop_ {
                                    options: [15, 20, 30, 45, 60, 90].map(m => [`Every ${win.minutesLabel(m)}`, m])
                                    current: Cae.WinSettings.distroBreakEvery
                                    onPicked: v => Cae.WinSettings.distroBreakEvery = v
                                }
                            }
                        }
                    }

                    // ================= Mouse & Touchpad
                    Page_ {
                        name: "mouse"
                        Head_ { text: "Mouse" }
                        Group {
                            Row_ {
                                label: "Primary Button"
                                sub: "The button you click with"
                                Row {
                                    spacing: 0
                                    Repeater {
                                        model: [["Left", 0], ["Right", 1]]
                                        Rectangle {
                                            required property var modelData
                                            width: 70; height: 32; radius: 8
                                            color: (win.input.swapButtons ? 1 : 0) === modelData[1] ? Adw.accent : Qt.rgba(1, 1, 1, 0.08)
                                            Text { anchors.centerIn: parent; text: modelData[0]; color: Adw.text; font.family: Adw.font; font.pixelSize: 13 }
                                            MouseArea { anchors.fill: parent; onClicked: win.setInput("swapButtons", modelData[1]) }
                                        }
                                    }
                                }
                            }
                            Row_ {
                                label: "Pointer Speed"
                                Slider_ { implicitWidth: 240; value: ((win.input.mouseSpeed ?? 10) - 1) / 19; onMoved: v => win.setInput("mouseSpeed", Math.round(1 + v * 19)) }
                            }
                            Row_ {
                                label: "Mouse Acceleration"
                                sub: "The pointer goes further when you move the mouse fast"
                                GSwitch { checked: !!win.input.acceleration; onToggled: win.setInput("acceleration", win.input.acceleration ? 0 : 1) }
                            }
                            Row_ {
                                label: "Scroll Speed"
                                sub: `${win.input.wheelLines ?? 3} lines per notch`
                                Slider_ { implicitWidth: 240; value: ((win.input.wheelLines ?? 3) - 1) / 19; onMoved: v => win.setInput("wheelLines", Math.round(1 + v * 19)) }
                            }
                            Row_ {
                                label: "Double-Click Delay"
                                sub: `${win.input.doubleClickMs ?? 500} ms`
                                Slider_ { implicitWidth: 240; value: ((win.input.doubleClickMs ?? 500) - 200) / 700; onMoved: v => win.setInput("doubleClickMs", Math.round(200 + v * 700)) }
                            }
                        }
                        Group {
                            Row_ { label: "Touchpad"; sub: "Only on PCs with a precision touchpad"; external: true; onClicked: win.openWin("devices-touchpad") }
                        }
                    }

                    // ================= Keyboard
                    Page_ {
                        name: "keyboard"
                        Head_ { text: "Input Sources" }
                        Group {
                            Repeater {
                                model: Win.Keyboard.layoutCodes
                                Row_ {
                                    required property var modelData
                                    label: String((modelData === Win.Keyboard.currentLayoutCode ? Win.Keyboard.currentLayoutName : "") || modelData || "")
                                    Text { visible: !!modelData && modelData === Win.Keyboard.currentLayoutCode; text: "check"; font.family: Adw.icons; font.pixelSize: 20; color: Adw.accent }
                                    onClicked: Win.Keyboard.activateLayout(modelData)
                                }
                            }
                            Row_ { label: "Add Input Source…"; external: true; onClicked: win.openWin("regionlanguage") }
                        }
                        Head_ { text: "Typing" }
                        Group {
                            Row_ {
                                label: "Repeat Keys Delay"
                                sub: "How long a key is held before it repeats"
                                Slider_ { implicitWidth: 240; value: 1 - (win.input.keyDelay ?? 1) / 3; onMoved: v => win.setInput("keyDelay", Math.round((1 - v) * 3)) }
                            }
                            Row_ {
                                label: "Repeat Keys Speed"
                                Slider_ { implicitWidth: 240; value: (win.input.keySpeed ?? 31) / 31; onMoved: v => win.setInput("keySpeed", Math.round(v * 31)) }
                            }
                        }
                        Head_ { text: "Keyboard Shortcuts" }
                        Group {
                            Row_ { label: "Activities overview"; Key_ { text: "Super" } }
                            Row_ { label: "Show all apps"; Key_ { text: "Super + A" } }
                            Row_ { label: "System Monitor"; Key_ { text: "Ctrl + Shift + Esc" } }
                            Row_ { label: "Lock screen"; Key_ { text: "Super + L" } }
                            Row_ { label: "Window shortcuts"; sub: "~/.config/niri/config.kdl (or hyprland.conf) - reload with: aetherwm reload" }
                        }
                    }

                    // ================= Color
                    Page_ {
                        name: "color"
                        Group {
                            Repeater {
                                model: Quickshell.screens
                                Row_ {
                                    required property var modelData
                                    label: modelData.name
                                    sub: `${modelData.width} × ${modelData.height} · ${modelData.model || "display"} · colour profile from the display driver`
                                }
                            }
                            Row_ { label: "Color Management"; external: true; onClicked: Quickshell.execDetached(["colorcpl.exe"]) }
                        }
                    }

                    // ================= Printers
                    Page_ {
                        name: "printers"
                        Group {
                            Row_ { visible: win.printers.length === 0; label: printerProc.running ? "Looking for printers…" : "No printers" }
                            Repeater {
                                model: win.printers
                                Row_ {
                                    required property var modelData
                                    label: modelData[0]
                                    sub: (modelData[2] === "True" ? "Default · " : "") + (modelData[1] || "")
                                }
                            }
                            Row_ { label: "Add Printer…"; external: true; onClicked: win.openWin("printers") }
                        }
                    }

                    // ================= Accessibility
                    Page_ {
                        name: "accessibility"
                        Head_ { text: "Seeing" }
                        Group {
                            Row_ {
                                label: "Reduce Animation"
                                sub: "The Debian desktop's own animations off (menus, overview, dots)"
                                GSwitch { checked: Cae.WinSettings.distroReduceMotion; onToggled: Cae.WinSettings.distroReduceMotion = !Cae.WinSettings.distroReduceMotion }
                            }
                            Row_ { label: "High Contrast"; external: true; onClicked: win.openWin("easeofaccess-highcontrast") }
                            Row_ { label: "Large Text"; external: true; onClicked: win.openWin("easeofaccess-display") }
                            Row_ { label: "Cursor Size"; external: true; onClicked: win.openWin("easeofaccess-mousepointer") }
                            Row_ { label: "Screen Reader"; sub: "Narrator reads what's on the screen"; external: true; onClicked: Quickshell.execDetached(["narrator.exe"]) }
                        }
                        Head_ { text: "Hearing" }
                        Group {
                            Row_ { label: "Visual Alerts"; sub: "Flash the screen when a sound plays"; external: true; onClicked: win.openWin("easeofaccess-audio") }
                        }
                        Head_ { text: "Typing" }
                        Group {
                            Row_ { label: "Screen Keyboard"; external: true; onClicked: Quickshell.execDetached(["osk.exe"]) }
                            Row_ { label: "Repeat Keys"; sub: "Keyboard"; chevron: true; onClicked: win.page = "keyboard" }
                            Row_ { label: "Sticky Keys and Slow Keys"; external: true; onClicked: win.openWin("easeofaccess-keyboard") }
                        }
                        Head_ { text: "Pointing & Clicking" }
                        Group {
                            Row_ { label: "Double-Click Delay"; sub: "Mouse & Touchpad"; chevron: true; onClicked: win.page = "mouse" }
                        }
                        Head_ { text: "Zoom" }
                        Group {
                            Row_ { label: "Zoom"; sub: "Magnifier · Super + Plus"; external: true; onClicked: Quickshell.execDetached(["magnify.exe"]) }
                        }
                    }

                    // ================= Privacy & Security
                    Page_ {
                        name: "privacy"
                        Group {
                            Row_ { label: "Location"; external: true; onClicked: win.openWin("privacy-location") }
                            Row_ { label: "Camera"; external: true; onClicked: win.openWin("privacy-webcam") }
                            Row_ { label: "Microphone"; external: true; onClicked: win.openWin("privacy-microphone") }
                            Row_ { label: "File History & Trash"; sub: "Empty the Recycle Bin and temporary files by themselves"; external: true; onClicked: win.openWin("storagesense") }
                            Row_ { label: "Device Security"; external: true; onClicked: win.openWin("windowsdefender:") }
                        }
                        Head_ { text: "Screen Lock" }
                        Group {
                            Row_ {
                                label: "Lock at Sign In"
                                sub: "Aether starts on the lock screen"
                                GSwitch { checked: Cae.WinSettings.lockOnStart; onToggled: Cae.WinSettings.lockOnStart = !Cae.WinSettings.lockOnStart }
                            }
                            Row_ {
                                label: "Blank Screen Delay"
                                sub: "Power"
                                chevron: true
                                onClicked: win.page = "power"
                            }
                            Row_ {
                                label: "Lock Screen Notifications"
                                GSwitch { checked: Cae.WinSettings.distroLockNotifs; onToggled: Cae.WinSettings.distroLockNotifs = !Cae.WinSettings.distroLockNotifs }
                            }
                            Row_ {
                                label: "Keep Awake"
                                sub: "While on, the screen doesn't blank or lock by itself"
                                GSwitch { checked: CaeState.RuntimeState.inhibitIdle; onToggled: CaeState.RuntimeState.inhibitIdle = !CaeState.RuntimeState.inhibitIdle }
                            }
                            Row_ {
                                label: "Lock Screen Now"
                                Text { text: "lock"; font.family: Adw.icons; font.pixelSize: 18; color: Adw.dim }
                                onClicked: Quickshell.execDetached([`${Cae.WinSettings.packageDir}/qs/qsw.exe`, "ipc", "-p", Quickshell.shellPath("shell.qml"), "call", "lock", "lock"])
                            }
                        }
                    }

                    // ================= System and its pages
                    Page_ {
                        name: "system"
                        Group {
                            Repeater {
                                model: win.systemPages
                                Row_ {
                                    required property var modelData
                                    label: modelData[1]
                                    chevron: true
                                    onClicked: win.page = modelData[0]
                                }
                            }
                        }
                    }
                    Page_ {
                        name: "region"
                        Group {
                            Row_ { label: "Language"; sub: Qt.locale().nativeLanguageName; external: true; onClicked: win.openWin("regionlanguage") }
                            Row_ { label: "Formats"; sub: `${Qt.locale().nativeTerritoryName} · ${Qt.formatDate(new Date(), Qt.locale().dateFormat(Locale.ShortFormat))}`; external: true; onClicked: win.openWin("regionformatting") }
                        }
                    }
                    Page_ {
                        name: "datetime"
                        Group {
                            Row_ { label: "Automatic Date & Time"; sub: "Set by Windows from the internet"; external: true; onClicked: win.openWin("dateandtime") }
                            Row_ { label: "Date & Time"; sub: Qt.formatDateTime(Cae.Time.date, Cae.WinSettings.distroClock === "12h" ? "dddd d MMMM yyyy, h:mm AP" : "dddd d MMMM yyyy, hh:mm") }
                            Row_ { label: "Time Zone"; sub: Qt.formatDateTime(Cae.Time.date, "t"); external: true; onClicked: win.openWin("dateandtime") }
                        }
                        Group {
                            Row_ {
                                label: "Time Format"
                                Row {
                                    Repeater {
                                        model: [["24-hour", "24h"], ["AM / PM", "12h"]]
                                        Rectangle {
                                            required property var modelData
                                            width: 80; height: 32; radius: 8
                                            color: (Cae.WinSettings.distroClock || "24h") === modelData[1] ? Adw.accent : Qt.rgba(1, 1, 1, 0.08)
                                            Text { anchors.centerIn: parent; text: modelData[0]; color: Adw.text; font.family: Adw.font; font.pixelSize: 13 }
                                            MouseArea { anchors.fill: parent; onClicked: Cae.WinSettings.distroClock = modelData[1] }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Page_ {
                        name: "users"
                        Group {
                            Row_ { label: Quickshell.env("USERNAME") || "User"; sub: "Your account · Administrator"; external: true; onClicked: win.openWin("yourinfo") }
                            Row_ { label: "Other Users"; external: true; onClicked: win.openWin("otherusers") }
                        }
                    }
                    Page_ {
                        name: "remote"
                        Group {
                            Row_ { label: "Remote Desktop"; sub: "Windows' Remote Desktop (off unless you turned it on there)"; external: true; onClicked: win.openWin("remotedesktop") }
                        }
                    }
                    Page_ {
                        name: "ssh"
                        Group {
                            Row_ { label: "Secure Shell"; sub: "OpenSSH Server is a Windows optional feature"; external: true; onClicked: win.openWin("optionalfeatures") }
                        }
                    }
                    Page_ {
                        name: "updates"
                        Group {
                            Row_ { label: "Aether"; sub: "Updates come from Aether's releases (github.com/crszy/aether)" }
                            Row_ { label: "Windows Update"; external: true; onClicked: win.openWin("windowsupdate") }
                        }
                    }
                    Page_ {
                        name: "about"
                        Column {
                            Layout.fillWidth: true
                            spacing: 6
                            Image {
                                anchors.horizontalCenter: parent.horizontalCenter
                                source: Quickshell.shellPath("distros/debian/debian-swirl.svg")
                                sourceSize: Qt.size(120, 120)
                            }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Debian look"; color: Adw.text; font.family: Adw.font; font.pixelSize: 22; font.weight: Font.Bold }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Aether on Windows"; color: Adw.dim; font.family: Adw.font; font.pixelSize: 14 }
                        }
                        Group {
                            Row_ { label: "Device Name"; sub: Quickshell.env("COMPUTERNAME") || "" }
                            Row_ { label: "Operating System"; sub: "Windows, with the Aether shell" }
                            Row_ { label: "Desktop"; sub: "GNOME 48 look (Aether)" }
                            // the way back to the other shells and looks
                            Row_ { label: "Aether Settings"; sub: "Other shells, Mix & match, distro looks"; external: true; onClicked: AS.AetherSettings.open("") }
                        }
                        Head_ { text: "Hardware" }
                        Group {
                            Row_ { label: "Processor"; sub: `${Quickshell.env("PROCESSOR_IDENTIFIER") || ""} × ${win.aboutStats.cpus ?? ""}` }
                            Row_ { label: "Memory"; sub: win.aboutStats.total ? `${(win.aboutStats.total / 1073741824).toFixed(1)} GiB` : "" }
                            Row_ {
                                label: "Displays"
                                sub: Win.Displays.displays.filter(d => d.active).map(d => `${d.name} ${d.resolutionWidth} × ${d.resolutionHeight} @ ${d.refreshRate} Hz`).join(", ")
                            }
                            Row_ { label: "System Details"; external: true; onClicked: win.openWin("about") }
                        }
                    }
                }
            }
        }

        // ---- the dropdown menu
        MouseArea {
            anchors.fill: parent
            visible: rootItem.menuItems.length > 0
            onClicked: rootItem.menuItems = []
        }
        Rectangle {
            visible: rootItem.menuItems.length > 0
            x: rootItem.menuPos.x
            y: rootItem.menuPos.y
            width: Math.max(200, menuCol.implicitWidth + 12)
            height: Math.min(menuCol.implicitHeight + 12, 360)
            radius: 12
            color: "#383838"
            border.color: Qt.rgba(0, 0, 0, 0.4)
            Flickable {
                anchors.fill: parent
                anchors.margins: 6
                contentHeight: menuCol.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: menuCol
                    width: parent.width
                    Repeater {
                        model: rootItem.menuItems
                        Rectangle {
                            required property var modelData
                            width: menuCol.width
                            implicitWidth: menuText.implicitWidth + 56
                            height: 36
                            radius: 6
                            color: itemArea.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                            Text {
                                id: menuText
                                x: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData[0]
                                color: Adw.text
                                font.family: Adw.font
                                font.pixelSize: 14
                            }
                            Text {
                                visible: modelData[1] === rootItem.menuCurrent
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "check"
                                font.family: Adw.icons
                                font.pixelSize: 18
                                color: Adw.text
                            }
                            MouseArea {
                                id: itemArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    const cb = rootItem.menuCallback;
                                    rootItem.menuItems = [];
                                    if (cb) cb(modelData[1]);
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- "About Settings"
        Rectangle {
            anchors.fill: parent
            visible: win.aboutApp
            color: Qt.rgba(0, 0, 0, 0.5)
            MouseArea { anchors.fill: parent; onClicked: win.aboutApp = false }
            Rectangle {
                anchors.centerIn: parent
                width: 340
                height: 300
                radius: 14
                color: "#383838"
                MouseArea { anchors.fill: parent }
                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: "settings"; font.family: Adw.icons; font.pixelSize: 80; color: Adw.text }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Settings"; color: Adw.text; font.family: Adw.font; font.pixelSize: 22; font.weight: Font.Bold }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: "The GNOME 48 Settings look, for Aether"; color: Adw.dim; font.family: Adw.font; font.pixelSize: 13 }
                    Item { width: 1; height: 12 }
                    Pill { anchors.horizontalCenter: parent.horizontalCenter; text: "Close"; onClicked: win.aboutApp = false }
                }
            }
        }
    }

    // one panel's content
    component Page_: ColumnLayout {
        property string name
        visible: win.page === name
        Layout.fillWidth: true
        spacing: 18
    }
    // a group title above a boxed list
    component Head_: Text {
        Layout.topMargin: 4
        color: Adw.text
        font.family: Adw.font
        font.pixelSize: 15
        font.weight: Font.Bold
    }
    // a libadwaita boxed list
    component Group: Rectangle {
        default property alias rows: list.children
        Layout.fillWidth: true
        implicitHeight: list.implicitHeight
        radius: 12
        color: "#3a3a3a"
        clip: true
        Column {
            id: list
            width: parent.width
        }
    }
    component Row_: Rectangle {
        id: row
        property string label
        property string sub
        property bool chevron: false    // opens another panel
        property bool external: false   // opens a Windows settings page or app
        default property alias trailing: tail.children
        signal clicked
        width: parent ? parent.width : 0
        height: sub ? 62 : 52
        color: rowArea.containsMouse ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
        MouseArea { id: rowArea; anchors.fill: parent; hoverEnabled: true; onClicked: row.clicked() }
        Column {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.right: tail.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            Text { width: parent.width; text: row.label; color: Adw.text; font.family: Adw.font; font.pixelSize: 14; elide: Text.ElideRight }
            Text { width: parent.width; visible: row.sub.length > 0; text: row.sub; color: Adw.dim; font.family: Adw.font; font.pixelSize: 12; elide: Text.ElideRight }
        }
        Text {
            id: go
            visible: row.chevron || row.external
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: row.chevron ? "chevron_right" : "open_in_new"
            font.family: Adw.icons
            font.pixelSize: row.chevron ? 20 : 17
            color: Adw.dim
        }
        Row {
            id: tail
            anchors.right: go.visible ? go.left : parent.right
            anchors.rightMargin: go.visible ? 10 : 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(0, 0, 0, 0.25) }
    }
    component Slider_: Item {
        id: sl
        property real value
        signal moved(real v)
        implicitHeight: 24
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 6
            radius: 3
            color: Qt.rgba(1, 1, 1, 0.18)
            Rectangle { width: Math.max(0, Math.min(1, sl.value)) * parent.width; height: parent.height; radius: 3; color: Adw.accent }
        }
        Rectangle {
            width: 20; height: 20; radius: 10; color: "#ffffff"
            anchors.verticalCenter: parent.verticalCenter
            x: Math.max(0, Math.min(1, sl.value)) * (parent.width - width)
        }
        MouseArea {
            anchors.fill: parent
            function set(mx: real): void { sl.moved(Math.max(0, Math.min(1, mx / width))); }
            onPressed: m => set(m.x)
            onPositionChanged: m => { if (pressed) set(m.x); }
        }
    }
    // a dropdown button (libadwaita combo row); the list opens in the window's menu
    component Drop_: Rectangle {
        id: dd
        property var options: []   // [[label, value], ...]
        property var current
        signal picked(var value)
        readonly property string currentLabel: (options.find(o => o[1] === current) ?? [String(current ?? "")])[0]
        implicitWidth: Math.max(130, ddText.implicitWidth + 44)
        implicitHeight: 34
        radius: 8
        color: ddArea.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.07)
        Text {
            id: ddText
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            text: dd.currentLabel
            color: Adw.text
            font.family: Adw.font
            font.pixelSize: 13
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: "expand_more"
            font.family: Adw.icons
            font.pixelSize: 18
            color: Adw.dim
        }
        MouseArea {
            id: ddArea
            anchors.fill: parent
            hoverEnabled: true
            enabled: dd.options.length > 0
            onClicked: {
                let p = dd.parent;   // the window's menu lives on the root item
                while (p && typeof p.openMenu !== "function")
                    p = p.parent;
                if (p)
                    p.openMenu(dd, dd.options, dd.current, v => dd.picked(v));
            }
        }
    }
    component Radio: Rectangle {
        property bool checked
        implicitWidth: 22
        implicitHeight: 22
        radius: 11
        color: "transparent"
        border.width: 2
        border.color: checked ? Adw.accent : Qt.rgba(1, 1, 1, 0.35)
        Rectangle { anchors.centerIn: parent; width: 10; height: 10; radius: 5; color: Adw.accent; visible: parent.checked }
    }
    component Pill: Rectangle {
        id: pill
        property string text
        property bool suggested: false
        signal clicked
        implicitWidth: pillText.implicitWidth + 28
        implicitHeight: 34
        radius: 8
        color: suggested ? Adw.accent : pillArea.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.1)
        Text { id: pillText; anchors.centerIn: parent; text: pill.text; color: Adw.text; font.family: Adw.font; font.pixelSize: 13; font.weight: Font.Bold }
        MouseArea { id: pillArea; anchors.fill: parent; hoverEnabled: true; onClicked: pill.clicked() }
    }
    component Key_: Rectangle {
        property string text
        implicitWidth: keyText.implicitWidth + 16
        implicitHeight: 26
        radius: 6
        color: Qt.rgba(1, 1, 1, 0.1)
        Text { id: keyText; anchors.centerIn: parent; text: parent.text; color: Adw.text; font.family: Adw.font; font.pixelSize: 12 }
    }
    component IconButton: Rectangle {
        id: ib
        property string icon
        property bool active
        signal clicked
        width: 34
        height: 34
        radius: 8
        color: active ? Qt.rgba(1, 1, 1, 0.14) : ibArea.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        Text { anchors.centerIn: parent; text: ib.icon; font.family: Adw.icons; font.pixelSize: 19; color: Adw.text }
        MouseArea { id: ibArea; anchors.fill: parent; hoverEnabled: true; onClicked: ib.clicked() }
    }
    component GIconBtn: Text {
        id: gib
        property string icon
        signal clicked
        text: icon
        font.family: Adw.icons
        font.pixelSize: 19
        color: Adw.dim
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: gib.clicked() }
    }
}
