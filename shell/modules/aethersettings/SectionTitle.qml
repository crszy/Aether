// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

// A section: a title and a short line under it.
Column {
    property string text: ""
    property string sub: ""
    width: parent ? parent.width : 400
    topPadding: 18
    bottomPadding: 2
    spacing: 4
    T { kind: "title"; text: parent.text; font.pixelSize: 17; font.weight: Font.DemiBold; color: M3.ink }
    T { kind: "reading"; visible: parent.sub.length > 0; text: parent.sub; color: M3.inkDim; font.pixelSize: 12; width: parent.width; wrapMode: Text.WordWrap }
}
