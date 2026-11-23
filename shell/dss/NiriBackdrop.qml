// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Wayland
import QtQuick
import qs.services as Cae
import QtQuick.Effects

// niri only: the overview's backdrop (SUPER + Tab) is your wallpaper, blurred and a little darker, behind the
// zoomed-out workspaces (the workspaces themselves keep the sharp wallpaper). A layer rule from rice-niri puts this
// surface into the backdrop. It is drawn once per wallpaper: nothing in it moves.
PanelWindow {
    screen: Mon.screen
    id: bd
    property string path: ""
    visible: Wm.niri && path !== "" && !Cae.WinSettings.weShown
    color: "#101114"
    anchors { top: true; bottom: true; left: true; right: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Background
    WlrLayershell.namespace: "rice-backdrop"
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    mask: Region { }

    Image {
        id: img
        anchors.fill: parent
        visible: false
        source: bd.path !== "" ? "file:///" + bd.path : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
        // blurred anyway: a small picture is enough (and cheap)
        sourceSize.width: Math.max(320, Math.round(bd.width / 3))
    }
    MultiEffect {
        anchors.fill: parent
        source: img
        visible: img.status === Image.Ready
        blurEnabled: true
        blur: 1.0
        blurMax: 48
        saturation: 0.15
        brightness: -0.12
    }
}
