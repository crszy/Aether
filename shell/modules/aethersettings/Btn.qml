// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

// A Material 3 button. variant: "tonal" (default), "filled" (same as primary: true), "outlined", "text", "danger".
// The corners close a little while you press it.
Rectangle {
    id: b
    property string text: ""
    property string icon: ""
    property string sym: ""          // a Google icon by name (falls back to icon)
    property bool primary: false
    property string variant: primary ? "filled" : "tonal"
    signal clicked()
    readonly property color ink: variant === "filled" ? M3.inkPrimary
                               : variant === "danger" ? M3.inkErrorC
                               : (variant === "outlined" || variant === "text") ? M3.primary : M3.inkSecondaryC
    width: Math.max(variant === "text" ? 64 : 120, row.implicitWidth + 36)
    height: 40
    radius: m.pressed ? 12 * M3.shapeK : 20 * M3.shapeK
    Behavior on radius { NumberAnimation { duration: 160 } }
    color: variant === "filled" ? (m.containsMouse ? Qt.lighter(M3.primary, 1.08) : M3.primary)
         : variant === "danger" ? (m.containsMouse ? Qt.lighter(M3.errorContainer, 1.15) : M3.errorContainer)
         : variant === "outlined" || variant === "text" ? (m.containsMouse ? M3.alpha(M3.primary, 0.1) : "transparent")
         : (m.containsMouse ? M3.surfaceHighest : M3.secondaryContainer)
    border.width: variant === "outlined" ? 1 : 0
    border.color: M3.outline
    Behavior on color { ColorAnimation { duration: 120 } }
    scale: m.pressed ? 0.97 : 1
    Behavior on scale { NumberAnimation { duration: 90 } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Icon { visible: b.icon.length > 0 || b.sym.length > 0; name: b.sym; glyph: b.icon; size: 16; color: b.ink; anchors.verticalCenter: parent.verticalCenter }
        T { text: b.text; font.pixelSize: 13; font.weight: Font.Medium; color: b.ink; anchors.verticalCenter: parent.verticalCenter }
    }
    MouseArea {
        id: m
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: b.clicked()
    }
}
