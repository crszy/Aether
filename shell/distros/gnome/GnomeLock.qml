import QtQuick
import QtQuick.Effects
import Quickshell
import qs.services as Cae
import "../catalog.js" as Catalog

// GNOME's lock screen: the blurred wallpaper, the date and a big clock; the first key slides the clock up and
// shows the user's name and the password field. Typing goes to the password check by itself (LockSurface):
// this shows pam.buffer as dots and pam.lockMessage.
Item {
    id: root

    property var pam
    property var lock
    property var screen

    readonly property var distro: Catalog.get(Cae.WinSettings.distro)
    readonly property int typed: pam?.buffer?.length ?? 0
    property bool prompt: typed > 0
    onTypedChanged: if (typed > 0) prompt = true

    Image {
        id: wall
        anchors.fill: parent
        source: root.distro?.wallpaper ? Quickshell.shellPath(`distros/${root.distro.wallpaper}`) : ""
        sourceSize: Qt.size(root.width / 4, root.height / 4)
        fillMode: Image.PreserveAspectCrop
        visible: false
    }
    MultiEffect {
        anchors.fill: parent
        source: wall
        blurEnabled: true
        blur: 1
        blurMax: 64
        brightness: -0.25
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.prompt = true
    }

    // ---- clock (slides up when the prompt shows)
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.prompt ? parent.height * 0.12 : parent.height * 0.3
        opacity: root.prompt ? 0 : 1
        spacing: 0
        Behavior on y { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 250 } }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatDate(Cae.Time.date, "dddd d MMMM")
            color: "#ffffff"
            font.family: Adw.font
            font.pixelSize: 26
            font.weight: Font.Bold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatTime(Cae.Time.date, Cae.WinSettings.distroClock === "12h" ? "h:mm" : "hh:mm")
            color: "#ffffff"
            font.family: Adw.font
            font.pixelSize: 120
            font.weight: Font.Bold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            topPadding: 40
            text: "Click or press a key to unlock"
            color: "#ffffff"
            opacity: 0.8
            font.family: Adw.font
            font.pixelSize: 16
        }
        // GNOME's lock screen notifications (Settings > Notifications > Lock Screen Notifications)
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            topPadding: 28
            spacing: 8
            visible: Cae.WinSettings.distroLockNotifs
            Repeater {
                model: Cae.WinSettings.distroLockNotifs ? Cae.Notifs.notClosed.slice(-3).reverse() : []
                Rectangle {
                    required property var modelData
                    width: 380
                    height: 54
                    radius: 14
                    color: Qt.rgba(0, 0, 0, 0.35)
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 14
                        anchors.verticalCenter: parent.verticalCenter
                        Text { width: parent.width; text: modelData.appName || "Notification"; color: Qt.rgba(1, 1, 1, 0.7); font.family: Adw.font; font.pixelSize: 12; elide: Text.ElideRight }
                        Text { width: parent.width; text: modelData.summary; color: "#ffffff"; font.family: Adw.font; font.pixelSize: 14; font.weight: Font.Bold; elide: Text.ElideRight }
                    }
                }
            }
        }
    }

    // ---- the prompt
    Column {
        anchors.centerIn: parent
        spacing: 18
        opacity: root.prompt ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 250 } }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 128
            height: 128
            radius: 64
            color: Qt.rgba(1, 1, 1, 0.15)
            Text {
                anchors.centerIn: parent
                text: "person"
                font.family: Adw.icons
                font.pixelSize: 80
                color: "#ffffff"
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Quickshell.env("USERNAME") || "User"
            color: "#ffffff"
            font.family: Adw.font
            font.pixelSize: 22
            font.weight: Font.Bold
        }
        Rectangle {
            id: field
            anchors.horizontalCenter: parent.horizontalCenter
            width: 300
            height: 40
            radius: 20
            color: Qt.rgba(1, 1, 1, 0.15)
            border.color: Adw.accent
            border.width: 2
            Text {
                anchors.centerIn: parent
                visible: root.typed === 0
                text: "Password"
                color: Qt.rgba(1, 1, 1, 0.5)
                font.family: Adw.font
                font.pixelSize: 15
            }
            Row {
                anchors.centerIn: parent
                spacing: 6
                Repeater {
                    model: Math.min(root.typed, 24)
                    Rectangle { width: 8; height: 8; radius: 4; color: "#ffffff" }
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: text.length > 0
            text: root.pam?.lockMessage ?? ""
            color: "#ffffff"
            opacity: 0.85
            font.family: Adw.font
            font.pixelSize: 14
        }
    }
}
