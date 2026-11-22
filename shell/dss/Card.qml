// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// The background of a desktop widget; its colors come from the widget's palette (WPal.qml).
//   DarkShadow: frosted glass over your blurred wallpaper (or a plain dark card, or solid when blur is off / Light mode)
//   Google:     a soft card tinted with your theme color, rounder corners
//   Outline:    no fill, only a thin line; the text sits right on the wallpaper
Rectangle {
    property real k: 1     // 0 = no background, 1 = normal
    property var pal: null
    readonly property bool glass: Look.blur && !Look.flat
    radius: pal ? pal.radius : 26
    // tell the widget's window about this card (niri blurs behind the cards only)
    property var holderItem: null
    Component.onCompleted: {
        for (let p = parent; p; p = p.parent) if (p.widgetHolder === true) { holderItem = p; p.cards = p.cards.concat([this]); break }
    }
    Component.onDestruction: if (holderItem) { const me = this; holderItem.cards = holderItem.cards.filter(c => c !== me) }
    color: pal ? pal.cardColor : Qt.rgba(0.10, 0.10, 0.115, 0.46 * k)
    border.width: pal ? pal.edgeWidth : 1
    border.color: pal ? pal.edge : Qt.rgba(1, 1, 1, 0.10 * k)
    // a soft shadow under the card (Settings > Desktop > Windows > Shell shadows)
    Shadow { z: -1; anchors.fill: parent; radius: parent.radius; on: Look.shadows && Look.shadowWidgets && parent.k > 0.02 && parent.color.a > 0.05
        size: Look.shadowSize * 0.8; strength: Look.shadowStrength * 0.8 }
    // a soft light along the top edge of the glass
    Rectangle {
        visible: (pal ? pal.shine : parent.glass) && parent.k > 0.02
        anchors.fill: parent
        radius: parent.radius
        opacity: 0.6 * parent.k
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.07) }
            GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }
}
