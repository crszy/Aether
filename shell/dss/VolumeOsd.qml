// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Wayland
import Quickshell.Services.Pipewire
import QtQuick

// How loud it is: a small pill near the bottom of the screen for a moment whenever the volume or mute changes
// (volume keys, headphone buttons, the bar, the sound menu). The microphone shows when you mute or unmute it.
PanelWindow {
    screen: Mon.screen
    id: osd
    readonly property var sink: Pipewire.defaultAudioSink
    readonly property var source: Pipewire.defaultAudioSource
    PwObjectTracker { objects: [osd.sink, osd.source] }
    readonly property real vol: Vol.of(sink)
    readonly property bool muted: sink && sink.audio ? sink.audio.muted : false
    readonly property bool micMuted: source && source.audio ? source.audio.muted : false

    property bool ready: false          // nothing shows while the shell starts
    Timer { running: true; interval: 2500; onTriggered: osd.ready = true }
    property bool mic: false            // showing the microphone instead of the speakers
    property bool shown: false
    property real t: 0
    Behavior on t { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    onShownChanged: t = shown ? 1 : 0
    property bool islandTakes: false    // the Dynamic bar's island shows it instead
    function pop(isMic) { if (!ready || islandTakes) return; mic = isMic; shown = true; hideTimer.restart() }
    Timer { id: hideTimer; interval: 1400; onTriggered: osd.shown = false }
    onVolChanged: pop(false)
    onMutedChanged: pop(false)
    onMicMutedChanged: pop(true)
    onSinkChanged: { ready = false; settle.restart() }
    Timer { id: settle; interval: 600; onTriggered: osd.ready = true }

    visible: shown || t > 0.01
    // niri: blur behind it (Hyprland blurs this layer with its own rule)
    NiriBlur { win: osd; items: [osdBg]; radius: osdBg.radius; on: osd.t > 0.98 }
    color: "transparent"
    anchors { bottom: true }
    margins { bottom: 110 }
    implicitWidth: 280
    implicitHeight: 56
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-osd"
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    mask: Region { }

    Rectangle {
        id: osdBg
        anchors.fill: parent
        radius: height / 2
        color: Qt.rgba(0.05, 0.05, 0.05, Look.flat ? 1 : 0.62)
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.09)
        opacity: osd.t
        scale: 0.9 + 0.1 * osd.t
        MIcon {
            x: 20; anchors.verticalCenter: parent.verticalCenter; size: 22; color: "#F6F5F4"
            name: osd.mic ? (osd.micMuted ? "mic_off" : "mic") : (osd.muted ? "volume_off" : (osd.vol < 0.34 ? "volume_mute" : (osd.vol < 0.67 ? "volume_down" : "volume_up")))
            glyph: String.fromCodePoint(osd.mic ? (osd.micMuted ? 0xF036D : 0xF036C) : (osd.muted ? 0xF0581 : (osd.vol < 0.34 ? 0xF057F : (osd.vol < 0.67 ? 0xF0580 : 0xF057E))))
        }
        // speakers: a bar and the percent; microphone: on / off
        Item {
            visible: !osd.mic
            x: 56; width: parent.width - 56 - 70; height: parent.height
            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 6; radius: 3; color: Qt.rgba(1, 1, 1, 0.14) }
            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: Math.max(6, parent.width * Math.min(1, osd.vol)); height: 6; radius: 3
                color: osd.muted ? Qt.rgba(1, 1, 1, 0.35) : "#F6F5F4"
                Behavior on width { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } } }
        }
        Text {
            visible: !osd.mic
            anchors.right: parent.right; anchors.rightMargin: 20; anchors.verticalCenter: parent.verticalCenter
            text: osd.muted ? Tr.t("Muted") : Math.round(osd.vol * 100) + "%"
            color: "#F6F5F4"; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold
        }
        Text {
            visible: osd.mic
            x: 56; anchors.verticalCenter: parent.verticalCenter
            text: osd.micMuted ? Tr.t("Microphone off") : Tr.t("Microphone on")
            color: "#F6F5F4"; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold
        }
    }
}
