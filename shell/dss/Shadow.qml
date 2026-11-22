// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// A soft shadow under one of the shell's own parts (Settings > Desktop > Windows > Shell shadows): a few
// rounded layers that fade out, cheap to draw. Put it before the thing it belongs to, with the same geometry.
Item {
    id: sh
    property real radius: 20
    property bool on: Look.shadows
    property real size: Look.shadowSize
    property real strength: Look.shadowStrength
    property real offsetY: Math.round(size * 0.25)
    // with blur on, Hyprland blurs every pixel of the layer that is more opaque than the rule's ignore_alpha (0.1 to 0.3);
    // a strong shadow crossed it and left a lighter square of blur past the rounded corners. It is kept below that.
    readonly property real amp: Look.blur ? Math.min(strength * 0.16, 0.026) : strength * 0.16
    visible: on && strength > 0.01
    Repeater {
        model: 6
        delegate: Rectangle {
            readonly property real k: (index + 1) / 6
            x: -sh.size * k; y: -sh.size * k + sh.offsetY
            width: sh.width + 2 * sh.size * k; height: sh.height + 2 * sh.size * k
            radius: sh.radius + sh.size * k
            color: Qt.rgba(0, 0, 0, sh.amp * (1 - k * 0.8))
        }
    }
}
