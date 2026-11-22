import QtQuick
import QtQuick.Layouts
import Quickshell
import qs.services as Cae
import qs.modules.rice as Rice
import Caelestia.State as CaeState
import qs.components.effects

// GNOME's quick settings menu: a row of round buttons (screenshot, settings, lock, power), the volume and
// brightness sliders, then pill toggles two to a row.
Rectangle {
    id: root

    signal closeRequested
    signal powerRequested(string action)   // "restart" | "poweroff" | "logout": GNOME's confirm dialog for it
    signal monitorRequested
    signal settingsRequested(string page)

    property size screenSize: Qt.size(1920, 1080)   // the menu window covers the screen (origin 0,0)
    property bool powerOpen: false

    readonly property var monitor: Cae.Brightness.monitors.find(m => m.modelData === Quickshell.screens[0]) ?? Cae.Brightness.monitors[0] ?? null
    readonly property string qsw: Cae.WinSettings.packageDir ? `${Cae.WinSettings.packageDir}/qs/qsw.exe` : ""

    function ipc(args: var): void {
        if (qsw)
            Quickshell.execDetached([qsw, "ipc", "-p", Quickshell.shellPath("shell.qml"), "call"].concat(args));
    }

    width: 380
    height: content.implicitHeight + 32
    radius: Adw.menuRadius + 6
    color: Adw.glass ? "transparent" : Adw.popover
    border.color: Adw.glass ? Qt.rgba(1, 1, 1, 0.14) : Adw.border

    // Liquid Glass option: the menu becomes glass over the wallpaper
    Loader {
        anchors.fill: parent
        z: -1
        active: Adw.glass
        sourceComponent: LiquidGlass {
            radius: root.radius
            source: Adw.wallpaper
            screenSize: root.screenSize
            tint: Qt.rgba(0.06, 0.06, 0.07, 0.5)
        }
    }

    MouseArea { anchors.fill: parent }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 14

        // ---- top row
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            CircleButton { glyph: "screenshot_region"; onClicked: { root.closeRequested(); Quickshell.execDetached(["explorer.exe", "ms-screenclip:"]); } }
            CircleButton { glyph: "settings"; onClicked: root.settingsRequested("wifi") }
            CircleButton { glyph: "lock"; onClicked: { root.closeRequested(); root.ipc(["lock", "lock"]); } }
            CircleButton { glyph: "power_settings_new"; active: root.powerOpen; onClicked: root.powerOpen = !root.powerOpen }
        }

        // ---- GNOME's power submenu (Debian: "Log Out" always shown, even with one user)
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.powerOpen
            spacing: 2
            Text {
                Layout.leftMargin: 12
                Layout.bottomMargin: 4
                text: "Power Off"
                color: Adw.text
                font.family: Adw.font
                font.pixelSize: 15
                font.weight: Font.Bold
            }
            MenuItem { text: "Suspend"; onClicked: { root.closeRequested(); Quickshell.execDetached(["rundll32.exe", "powrprof.dll,SetSuspendState", "0,1,0"]); } }
            MenuItem { text: "Restart…"; onClicked: root.powerRequested("restart") }
            MenuItem { text: "Power Off…"; onClicked: root.powerRequested("poweroff") }
            Rectangle { Layout.fillWidth: true; Layout.margins: 6; implicitHeight: 1; color: Adw.border }
            MenuItem { text: "Log Out…"; onClicked: root.powerRequested("logout") }
        }

        // ---- sliders
        GSlider {
            glyph: Cae.Audio.muted ? "volume_off" : "volume_up"
            value: Cae.Audio.volume
            onMoved: v => Cae.Audio.setVolume(v)
            onGlyphClicked: if (Cae.Audio.sink?.audio) Cae.Audio.sink.audio.muted = !Cae.Audio.muted
        }
        GSlider {
            visible: !!root.monitor && root.monitor.available
            glyph: "light_mode"
            value: root.monitor?.brightness ?? 0
            onMoved: v => root.monitor.setBrightness(v)
        }

        // ---- toggles
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            Pill {
                glyph: Cae.Nmcli.activeInterface === "Ethernet" ? "lan" : "wifi"
                title: Cae.Nmcli.activeInterface === "Ethernet" ? "Wired" : "Wi-Fi"
                subtitle: Cae.Nmcli.activeConnection
                on: Cae.Nmcli.activeInterface === "Ethernet" ? Cae.Nmcli.isConnected : Cae.Nmcli.wifiEnabled
                onToggled: if (Cae.Nmcli.activeInterface !== "Ethernet") Cae.Nmcli.toggleWifi(null)
            }
            Pill {
                glyph: "notifications_off"
                title: "Do Not Disturb"
                on: CaeState.PersistentState.doNotDisturb
                onToggled: CaeState.PersistentState.doNotDisturb = !CaeState.PersistentState.doNotDisturb
            }
            Pill {
                glyph: "sports_esports"
                title: "Game Mode"
                on: CaeState.RuntimeState.gameMode
                onToggled: CaeState.RuntimeState.gameMode = !CaeState.RuntimeState.gameMode
            }
            Pill {
                glyph: "coffee"
                title: "Keep Awake"
                on: CaeState.RuntimeState.inhibitIdle
                onToggled: CaeState.RuntimeState.inhibitIdle = !CaeState.RuntimeState.inhibitIdle
            }
        }
    }

    component MenuItem: Rectangle {
        property string text
        signal clicked
        Layout.fillWidth: true
        implicitHeight: 36
        radius: 10
        color: miArea.containsMouse ? Adw.cardHover : "transparent"
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: parent.text
            color: Adw.text
            font.family: Adw.font
            font.pixelSize: 14
        }
        MouseArea {
            id: miArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: parent.clicked()
        }
    }

    component CircleButton: Rectangle {
        property string glyph
        property bool active
        signal clicked
        implicitWidth: 40
        implicitHeight: 40
        radius: 20
        color: active ? Qt.rgba(1, 1, 1, 0.24) : cbArea.containsMouse ? Adw.cardHover : Adw.card
        Text {
            anchors.centerIn: parent
            text: parent.glyph
            font.family: Adw.icons
            font.pixelSize: 19
            color: Adw.text
        }
        MouseArea {
            id: cbArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: parent.clicked()
        }
    }

    component Pill: Rectangle {
        id: pill
        property string glyph
        property string title
        property string subtitle
        property bool on
        signal toggled
        Layout.fillWidth: true
        implicitHeight: 52
        radius: 26
        color: on ? (pArea.containsMouse ? Qt.lighter(Adw.accent, 1.1) : Adw.accent) : (pArea.containsMouse ? Adw.cardHover : Adw.card)
        Behavior on color { ColorAnimation { duration: 150 } }
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: pill.glyph
                font.family: Adw.icons
                font.pixelSize: 19
                color: Adw.text
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 40
                Text {
                    width: parent.width
                    text: pill.title
                    color: Adw.text
                    font.family: Adw.font
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: pill.subtitle.length > 0
                    text: pill.subtitle
                    color: Adw.text
                    opacity: 0.8
                    font.family: Adw.font
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }
        MouseArea {
            id: pArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: pill.toggled()
        }
    }

    component GSlider: RowLayout {
        id: sl
        property string glyph
        property real value
        signal moved(real v)
        signal glyphClicked
        Layout.fillWidth: true
        spacing: 10
        Text {
            text: sl.glyph
            font.family: Adw.icons
            font.pixelSize: 20
            color: Adw.text
            MouseArea {
                anchors.fill: parent
                onClicked: sl.glyphClicked()
            }
        }
        Item {
            Layout.fillWidth: true
            implicitHeight: 24
            Rectangle {
                id: track
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 6
                radius: 3
                color: Qt.rgba(1, 1, 1, 0.18)
                Rectangle {
                    width: Math.max(0, Math.min(1, sl.value)) * parent.width
                    height: parent.height
                    radius: 3
                    color: Adw.accent
                }
            }
            Rectangle {
                width: 20
                height: 20
                radius: 10
                color: "#ffffff"
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
    }
}
