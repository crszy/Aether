import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.services as Cae

// GNOME's OSD: a dark pill near the bottom of the screen with the icon and a level bar, for volume and brightness.
PanelWindow {
    id: win

    property string glyph: "volume_up"
    property real level: 0
    property bool ready

    function show(g: string, v: real): void {
        if (!ready)
            return;
        glyph = g;
        level = v;
        visible = true;
        hide.restart();
    }

    screen: Quickshell.screens.find(s => Cae.WinSettings.isMainScreen(s)) ?? Quickshell.screens[0]
    visible: false
    anchors.bottom: true
    margins.bottom: 90
    implicitWidth: 300
    implicitHeight: 52
    exclusionMode: ExclusionMode.Ignore
    color: "transparent"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "aether-gnome-osd"

    Timer {
        id: hide
        interval: 1500
        onTriggered: win.visible = false
    }
    Timer {   // don't pop up for the values read at start
        running: true
        interval: 1500
        onTriggered: win.ready = true
    }

    Connections {
        target: Cae.Audio
        function onVolumeChanged(): void {
            win.show(Cae.Audio.muted ? "volume_off" : "volume_up", Cae.Audio.volume);
        }
        function onMutedChanged(): void {
            win.show(Cae.Audio.muted ? "volume_off" : "volume_up", Cae.Audio.muted ? 0 : Cae.Audio.volume);
        }
    }
    Instantiator {
        model: Cae.Brightness.monitors
        delegate: Connections {
            required property var modelData
            target: modelData
            function onBrightnessChanged(): void {
                win.show("light_mode", modelData.brightness);
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Adw.popover
        border.color: Adw.border
        Text {
            id: icon
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: win.glyph
            font.family: Adw.icons
            font.pixelSize: 22
            color: Adw.text
        }
        Rectangle {
            anchors.left: icon.right
            anchors.leftMargin: 14
            anchors.right: parent.right
            anchors.rightMargin: 22
            anchors.verticalCenter: parent.verticalCenter
            height: 6
            radius: 3
            color: Qt.rgba(1, 1, 1, 0.18)
            Rectangle {
                width: Math.max(0, Math.min(1, win.level)) * parent.width
                height: parent.height
                radius: 3
                color: Adw.text
                Behavior on width { NumberAnimation { duration: 100 } }
            }
        }
    }
}
