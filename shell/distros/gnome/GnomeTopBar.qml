import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland
import Quickshell.Services.SystemTray
import Quickshell.Services.UPower
import qs.services as Cae
import qs.utils as CaeUtils
import Caelestia.State as CaeState
import qs.components.effects

// GNOME Shell's top bar (Debian 13 / GNOME 48): workspace dots on the left (they open the overview), the date
// and time in the middle (calendar + notifications menu), the system status icons on the right (quick settings).
PanelWindow {
    id: bar

    required property var distro
    property string menu: ""   // "" | "date" | "quick"
    property bool overviewOpen
    signal overviewRequested
    signal powerRequested(string action)
    signal monitorRequested
    signal settingsRequested(string page)

    anchors {
        top: true
        left: true
        right: true
    }
    exclusionMode: ExclusionMode.Auto
    implicitHeight: Adw.barHeight
    color: overviewOpen || Adw.glass ? "transparent" : Adw.panel
    WlrLayershell.layer: WlrLayer.Top
    WlrLayershell.namespace: "aether-gnome-bar"

    Component.onCompleted: Adw.accent = Cae.WinSettings.distroAccent || distro.accent

    // Hot corner (Settings > Multitasking): pushing the pointer into the top-left corner opens Activities
    MouseArea {
        z: 10
        width: 2
        height: 2
        visible: Cae.WinSettings.distroHotCorner
        hoverEnabled: true
        onEntered: if (!bar.overviewOpen && Cae.WinSettings.distroMode) bar.overviewRequested()
    }

    // Liquid Glass option: the bar strip becomes glass over the wallpaper (top of the screen)
    Loader {
        anchors.fill: parent
        z: -1
        active: Adw.glass && !bar.overviewOpen
        sourceComponent: LiquidGlass {
            radius: 0
            bezel: 10
            refraction: 8
            source: Adw.wallpaper
            screenSize: Qt.size(bar.screen?.width ?? 1920, bar.screen?.height ?? 1080)
            windowOrigin: Qt.point(0, 0)
            tint: Qt.rgba(0, 0, 0, 0.35)
        }
    }

    function toggle(name: string): void {
        menu = menu === name ? "" : name;
    }

    // ---- left: workspace indicator (GNOME 45+: a wide pill for the current workspace, dots for the others)
    PanelButton {
        id: activities
        anchors.left: parent.left
        anchors.leftMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        active: bar.overviewOpen
        onClicked: bar.overviewRequested()

        Row {
            id: dots
            anchors.centerIn: parent
            spacing: 6

            readonly property var ids: {
                const used = (Cae.Hypr.workspaces?.values ?? []).map(w => w.id).filter(id => id > 0);
                // like GNOME's dynamic workspaces: the ones in use plus the current one, in order
                return [...new Set(used.concat([Cae.Hypr.activeWsId]))].sort((a, b) => a - b).slice(0, 12);
            }

            Repeater {
                model: dots.ids
                Rectangle {
                    required property int modelData
                    readonly property bool current: modelData === Cae.Hypr.activeWsId
                    width: current ? 36 : 7
                    height: 7
                    radius: 3.5
                    color: Adw.panelText
                    opacity: current ? 1 : 0.55
                    anchors.verticalCenter: parent.verticalCenter
                    Behavior on width { NumberAnimation { duration: Adw.animMs; easing.type: Easing.OutCubic } }
                }
            }
        }
        implicitWidth: dots.implicitWidth + 24
    }

    // ---- centre: date and time
    PanelButton {
        anchors.centerIn: parent
        active: bar.menu === "date"
        onClicked: bar.toggle("date")
        implicitWidth: clock.implicitWidth + 24

        Text {
            id: clock
            anchors.centerIn: parent
            text: Qt.formatDateTime(Cae.Time.date, "MMM d  ") + Qt.formatDateTime(Cae.Time.date, Cae.WinSettings.distroClock === "12h" ? "h:mm AP" : "hh:mm")
            color: Adw.panelText
            font.family: Adw.font
            font.pixelSize: 14
            font.weight: Font.Bold
        }
        Rectangle {   // unread notifications dot, like GNOME's
            visible: (Cae.Notifs.notClosed.length) > 0 && bar.menu !== "date"
            width: 5
            height: 5
            radius: 2.5
            color: Adw.panelText
            anchors.left: clock.right
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // ---- right, before the status area: app tray icons, like Debian's "AppIndicator and KStatusNotifierItem
    // Support" extension (one panel button per app). Using SystemTray here also starts Aether's tray window -
    // without Explorer nothing else owns Shell_TrayWnd, so apps' icons would have nowhere to go.
    Row {
        id: tray
        anchors.right: statusButton.left
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        Repeater {
            model: SystemTray.items.values.filter(i => i.status !== Status.Passive)
            PanelButton {
                id: trayButton
                required property SystemTrayItem modelData
                implicitWidth: Adw.barHeight + 4
                acceptRight: true
                onClicked: modelData.activate()
                onRightClicked: modelData.secondaryActivate()
                Image {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    sourceSize: Qt.size(32, 32)
                    source: CaeUtils.Icons.getTrayIcon(trayButton.modelData.id, trayButton.modelData.icon)
                    smooth: true
                }
            }
        }
    }

    // ---- right: system status (network, volume, battery, power)
    PanelButton {
        id: statusButton
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        active: bar.menu === "quick"
        onClicked: bar.toggle("quick")
        implicitWidth: status.implicitWidth + 24

        Row {
            id: status
            anchors.centerIn: parent
            spacing: 10
            GIcon {
                visible: CaeState.RuntimeState.gameMode
                text: "sports_esports"
            }
            GIcon {
                text: !Cae.Nmcli.isConnected ? "signal_wifi_off" : Cae.Nmcli.activeInterface === "Ethernet" ? "lan" : "wifi"
            }
            GIcon {
                text: Cae.Audio.muted ? "volume_off" : Cae.Audio.volume < 0.34 ? "volume_mute" : Cae.Audio.volume < 0.67 ? "volume_down" : "volume_up"
            }
            Row {   // battery, only on PCs that have one (Settings > Power > Show Battery Percentage)
                readonly property var dev: UPower.displayDevice
                visible: !!dev && dev.isLaptopBattery
                spacing: 3
                GIcon {
                    readonly property real p: parent.dev?.percentage ?? 0
                    text: !UPower.onBattery ? "battery_charging_full" : p > 0.9 ? "battery_full" : p > 0.6 ? "battery_5_bar"
                        : p > 0.35 ? "battery_3_bar" : p > 0.15 ? "battery_2_bar" : "battery_alert"
                }
                Text {
                    visible: Cae.WinSettings.distroBatteryPercent
                    anchors.verticalCenter: parent.verticalCenter
                    text: `${Math.round((parent.dev?.percentage ?? 0) * 100)}%`
                    color: Adw.panelText
                    font.family: Adw.font
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }
            }
            GIcon {
                text: "power_settings_new"
            }
        }
    }

    // ---- the menus: a see-through window over the screen (a click outside closes), only while open
    Loader {
        active: bar.menu !== ""
        sourceComponent: PanelWindow {
            screen: bar.screen
            anchors {
                top: true
                bottom: true
                left: true
                right: true
            }
            exclusionMode: ExclusionMode.Ignore
            color: "transparent"
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
            WlrLayershell.namespace: "aether-gnome-menu"

            MouseArea {
                anchors.fill: parent
                onClicked: bar.menu = ""
            }
            Item {
                anchors.fill: parent
                focus: true
                Keys.onEscapePressed: bar.menu = ""
            }

            Loader {
                id: menuLoader
                y: Adw.barHeight + 6
                x: bar.menu === "date" ? (parent.width - width) / 2 : parent.width - width - 8
                sourceComponent: bar.menu === "date" ? dateMenu : quickMenu
                opacity: 0
                scale: 0.96
                transformOrigin: bar.menu === "date" ? Item.Top : Item.TopRight
                Component.onCompleted: { opacity = 1; scale = 1; }
                Behavior on opacity { NumberAnimation { duration: 150 } }
                Behavior on scale { NumberAnimation { duration: Adw.animMs; easing.type: Easing.OutCubic } }
            }
        }
    }
    Component {
        id: dateMenu
        GnomeDateMenu {
            screenSize: Qt.size(bar.screen?.width ?? 1920, bar.screen?.height ?? 1080)
        }
    }
    Component {
        id: quickMenu
        GnomeQuickSettings {
            screenSize: Qt.size(bar.screen?.width ?? 1920, bar.screen?.height ?? 1080)
            onCloseRequested: bar.menu = ""
            onPowerRequested: action => { bar.menu = ""; bar.powerRequested(action); }
            onMonitorRequested: { bar.menu = ""; bar.monitorRequested(); }
            onSettingsRequested: page => { bar.menu = ""; bar.settingsRequested(page); }
        }
    }

    component GIcon: Text {
        color: Adw.panelText
        font.family: Adw.icons
        font.pixelSize: 17
    }

    component PanelButton: Rectangle {
        id: btn
        property bool active
        property bool acceptRight: false
        signal clicked
        signal rightClicked
        height: Adw.barHeight - 6
        radius: height / 2
        color: active ? Qt.rgba(1, 1, 1, 0.22) : area.containsMouse ? Adw.panelHover : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: btn.acceptRight ? (Qt.LeftButton | Qt.RightButton) : Qt.LeftButton
            onClicked: event => event.button === Qt.RightButton ? btn.rightClicked() : btn.clicked()
        }
    }
}
