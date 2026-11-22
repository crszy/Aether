// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

SettingRow {
    id: row
    property bool checked: false
    signal toggled(bool value)
    reserved: 96

    Tog {
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        checked: row.checked
        onToggled: function(v) { row.toggled(v) }
    }
}