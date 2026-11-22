import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland
import qs.services as Cae

// GNOME's end-session dialog: dims the screen. action "restart" / "poweroff" / "logout" = GNOME's confirm dialog
// for it (60 s countdown, then it happens, like GNOME); "" = a card with Suspend / Restart / Log Out / Power Off.
PanelWindow {
    id: win

    property string action: ""
    property int seconds: 60
    readonly property var info: ({
        restart: { title: "Restart", verb: "Restart", body: "The system will restart automatically in %1 seconds.", cmd: ["shutdown.exe", "/r", "/t", "0"] },
        poweroff: { title: "Power Off", verb: "Power Off", body: "The system will power off automatically in %1 seconds.", cmd: ["shutdown.exe", "/s", "/t", "0"] },
        logout: { title: "Log Out", verb: "Log Out", body: "%2 will be logged out automatically in %1 seconds.", cmd: ["shutdown.exe", "/l"] }
    })[action] ?? null

    signal dismissed

    Timer {
        running: !!win.info
        interval: 1000
        repeat: true
        onTriggered: if (--win.seconds <= 0) win.run(win.info.cmd)
    }

    function run(cmd: var): void {
        dismissed();
        Quickshell.execDetached(cmd);
    }

    screen: Quickshell.screens.find(s => Cae.WinSettings.isMainScreen(s)) ?? Quickshell.screens[0]
    anchors {
        top: true
        bottom: true
        left: true
        right: true
    }
    exclusionMode: ExclusionMode.Ignore
    color: Qt.rgba(0, 0, 0, 0.5)
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
    WlrLayershell.namespace: "aether-gnome-power"

    MouseArea {
        anchors.fill: parent
        onClicked: win.dismissed()
    }
    Item {
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: win.dismissed()
    }

    Rectangle {
        anchors.centerIn: parent
        width: 460
        height: col.implicitHeight + 48
        radius: 24
        color: Adw.popover
        border.color: Adw.border
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24
            spacing: 18

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: win.info ? win.info.title : "Power Off"
                color: Adw.text
                font.family: Adw.font
                font.pixelSize: 22
                font.weight: Font.Bold
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: win.info ? win.info.body.arg(win.seconds).arg(Quickshell.env("USERNAME") || "You")
                    : "Choose what to do. Unsaved work in open apps may be lost."
                color: Adw.dim
                font.family: Adw.font
                font.pixelSize: 14
            }
            RowLayout {   // GNOME's confirm dialog: Cancel + the action
                visible: !!win.info
                Layout.fillWidth: true
                spacing: 10
                DialogButton { text: "Cancel"; onClicked: win.dismissed() }
                DialogButton {
                    text: win.info ? win.info.verb : ""
                    destructive: win.action !== "logout"
                    onClicked: win.run(win.info.cmd)
                }
            }
            GridLayout {
                visible: !win.info
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 10
                rowSpacing: 10
                DialogButton { text: "Cancel"; onClicked: win.dismissed() }
                DialogButton { text: "Suspend"; onClicked: win.run(["rundll32.exe", "powrprof.dll,SetSuspendState", "0,1,0"]) }
                DialogButton { text: "Log Out"; onClicked: win.run(["shutdown.exe", "/l"]) }
                DialogButton { text: "Restart"; onClicked: win.run(["shutdown.exe", "/r", "/t", "0"]) }
                DialogButton { text: "Power Off"; destructive: true; Layout.columnSpan: 2; onClicked: win.run(["shutdown.exe", "/s", "/t", "0"]) }
            }
        }
    }

    component DialogButton: Rectangle {
        property string text
        property bool destructive
        signal clicked
        Layout.fillWidth: true
        implicitHeight: 42
        radius: 12
        color: destructive ? (dbArea.containsMouse ? "#e62d42" : "#c01c28") : (dbArea.containsMouse ? Adw.cardHover : Adw.card)
        Text {
            anchors.centerIn: parent
            text: parent.text
            color: Adw.text
            font.family: Adw.font
            font.pixelSize: 14
            font.weight: Font.Bold
        }
        MouseArea {
            id: dbArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: parent.clicked()
        }
    }
}
