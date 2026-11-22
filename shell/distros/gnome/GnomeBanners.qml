import QtQuick
import Quickshell
import Quickshell.Wayland
import Quickshell.Widgets
import qs.services as Cae

// GNOME notification banners: drop in under the top bar, centred, one card each (newest on top).
PanelWindow {
    id: win

    readonly property var shown: Cae.Notifs.popups.slice().reverse().slice(0, 3)

    screen: Quickshell.screens.find(s => Cae.WinSettings.isMainScreen(s)) ?? Quickshell.screens[0]
    visible: shown.length > 0
    anchors.top: true
    margins.top: Adw.barHeight + 6
    implicitWidth: 420
    implicitHeight: Math.max(1, column.implicitHeight)
    exclusionMode: ExclusionMode.Ignore
    color: "transparent"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "aether-gnome-banners"

    Column {
        id: column
        width: parent.width
        spacing: 8

        Repeater {
            model: win.shown
            Rectangle {
                id: banner
                required property var modelData
                width: column.width
                height: content.implicitHeight + 24
                radius: 16
                color: hover.containsMouse ? Adw.cardHover : Adw.popover
                border.color: Adw.border

                opacity: 0
                y: -10
                Component.onCompleted: { opacity = 1; y = 0; }
                Behavior on opacity { NumberAnimation { duration: 180 } }
                Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: banner.modelData.popup = false
                }

                Row {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 12

                    IconImage {
                        implicitSize: 36
                        anchors.verticalCenter: parent.verticalCenter
                        source: banner.modelData.image || Quickshell.iconPath(banner.modelData.appIcon, true) || Quickshell.shellPath("assets/app-generic.svg")
                    }
                    Column {
                        width: parent.width - 48
                        spacing: 2
                        Text {
                            width: parent.width
                            text: banner.modelData.summary
                            color: Adw.text
                            font.family: Adw.font
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            visible: text.length > 0
                            text: banner.modelData.body
                            color: Adw.text
                            opacity: 0.85
                            font.family: Adw.font
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                    }
                }
            }
        }
    }
}
