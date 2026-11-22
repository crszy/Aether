// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// A small label under a thing while the mouse is on it (the Task view's window titles).
Rectangle {
    id: tt
    property bool show: false
    property string text: ""
    property var pal: null
    visible: show && text !== ""
    z: 10
    width: Math.min(260, lab.implicitWidth + 16); height: 24; radius: 12
    anchors.horizontalCenter: parent.horizontalCenter
    y: parent.height + 4
    color: pal ? pal.surface : "#141414"
    border.width: 1; border.color: pal ? pal.line : Qt.rgba(1, 1, 1, 0.08)
    Text { id: lab; anchors.centerIn: parent; width: tt.width - 16; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter
        text: tt.text; color: tt.pal ? tt.pal.fg : "white"; font.family: Fonts.main; font.pixelSize: 11 }
}
