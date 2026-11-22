// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// A button of the toolbox: filled (your key color), tonal (the container color) or plain; with an icon, a label or
// both. It squeezes a little when pressed and its corners go from round to squarer while you hold it.
Rectangle {
    id: b
    property var pal
    property string label: ""
    property string icon: ""
    property string glyph: ""
    property string kind: "tonal"          // filled | tonal | plain | danger
    property bool on: false                // a chip that is chosen
    property real h: 40
    signal clicked()
    readonly property color base: kind === "filled" || on ? pal.primary : kind === "danger" ? "#ff6b6b" : kind === "plain" ? "transparent" : pal.card
    readonly property color ink: kind === "filled" || on ? pal.inkPrimary : kind === "danger" ? "#2a0a0a" : pal.fg
    implicitHeight: h
    implicitWidth: label === "" ? h : row.implicitWidth + (icon === "" ? 32 : 28)
    width: implicitWidth; height: implicitHeight
    radius: m.pressed ? Math.min(14, h / 3) : (on ? Math.min(14, h / 3) : h / 2)
    Behavior on radius { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    color: m.containsMouse ? (kind === "plain" && !on ? pal.cardHi : Qt.lighter(base, kind === "tonal" && !on ? 1.3 : 1.07)) : base
    Behavior on color { ColorAnimation { duration: 150 } }
    scale: m.pressed ? 0.95 : 1
    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    opacity: enabled ? 1 : 0.4
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8
        MIcon { visible: b.icon !== ""; anchors.verticalCenter: parent.verticalCenter; size: Math.round(b.h * 0.46); name: b.icon; glyph: b.glyph; color: b.ink }
        Text { visible: b.label !== ""; anchors.verticalCenter: parent.verticalCenter; text: b.label; color: b.ink
            font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.DemiBold }
    }
    MouseArea { id: m; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); b.clicked() } }
}
