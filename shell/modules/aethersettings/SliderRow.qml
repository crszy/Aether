// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

SettingRow {
    id: row
    property real from: 0
    property real to: 1
    property real step: 1
    property real value: 0
    property int decimals: 0
    property string unit: ""
    readonly property bool dragging: sld.dragging
    readonly property real shown: sld.shown      // the value while it is being dragged
    signal committed(real v)
    reserved: 300

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12
        T {
            text: (isFinite(sld.shown) ? Number(sld.shown).toFixed(row.decimals) : Number(row.from).toFixed(row.decimals)) + row.unit
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: 13
            color: M3.inkDim
        }
        Slide {
            id: sld
            from: row.from
            to: row.to
            step: row.step
            value: isFinite(row.value) ? row.value : row.from
            onReleased: function(v) { row.committed(v) }
        }
    }
}