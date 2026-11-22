import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.services as Cae

// The distro's wallpaper on the desktop layer (Debian 13: Ceratopsian, or Settings > Appearance > Background),
// and GNOME's desktop menu on a right click (the Desktop Icons NG one Debian users know).
PanelWindow {
    id: win

    required property var distro
    signal settingsRequested(string page)

    anchors {
        top: true
        bottom: true
        left: true
        right: true
    }
    exclusionMode: ExclusionMode.Ignore
    color: "#1d1d1d"
    WlrLayershell.layer: WlrLayer.Background
    WlrLayershell.namespace: "aether-distro-wallpaper"

    Image {
        anchors.fill: parent
        source: Cae.WinSettings.distroWallpaper ? "file:///" + Cae.WinSettings.distroWallpaper
            : win.distro?.wallpaper ? Quickshell.shellPath(`distros/${win.distro.wallpaper}`) : ""
        sourceSize: Qt.size(win.width, win.height)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        smooth: true
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) {
                menu.x = Math.min(mouse.x, win.width - menu.width - 4);
                menu.y = Math.min(mouse.y, win.height - menu.height - 4);
                menu.visible = true;
            } else {
                menu.visible = false;
            }
        }
    }

    Rectangle {
        id: menu
        visible: false
        width: 250
        height: items.implicitHeight + 12
        radius: 14
        color: Adw.popover
        border.color: Adw.border

        function run(action: var): void {
            visible = false;
            action();
        }

        Column {
            id: items
            x: 6
            y: 6
            width: parent.width - 12
            MenuItem { text: "New Folder"; onClicked: menu.run(() => Quickshell.execDetached(["cmd.exe", "/c", "mkdir", `${Quickshell.env("USERPROFILE")}\\Desktop\\New Folder`])) }
            MenuSep {}
            MenuItem { text: "Show Desktop in Files"; onClicked: menu.run(() => Qt.openUrlExternally("file:///" + `${Quickshell.env("USERPROFILE")}/Desktop`.replace(/\\/g, "/"))) }
            MenuItem { text: "Open in Terminal"; onClicked: menu.run(() => Quickshell.execDetached(["C:/Users/Administrator/alacritty.exe", "--working-directory", `${Quickshell.env("USERPROFILE")}\\Desktop`])) }
            MenuSep {}
            MenuItem { text: "Change Background…"; onClicked: menu.run(() => win.settingsRequested("appearance")) }
            MenuSep {}
            MenuItem { text: "Display Settings"; onClicked: menu.run(() => win.settingsRequested("displays")) }
            MenuItem { text: "Settings"; onClicked: menu.run(() => win.settingsRequested("wifi")) }
        }
    }

    component MenuItem: Rectangle {
        id: mi
        property string text
        signal clicked
        width: parent.width
        height: 34
        radius: 8
        color: miArea.containsMouse ? Qt.rgba(1, 1, 1, 0.1) : "transparent"
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: mi.text
            color: Adw.text
            font.family: Adw.font
            font.pixelSize: 14
        }
        MouseArea { id: miArea; anchors.fill: parent; hoverEnabled: true; onClicked: mi.clicked() }
    }
    component MenuSep: Item {
        width: parent.width
        height: 9
        Rectangle { anchors.centerIn: parent; width: parent.width - 12; height: 1; color: Adw.border }
    }
}
