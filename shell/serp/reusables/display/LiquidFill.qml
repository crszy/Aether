// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
import QtQuick
import "../../"

ShaderEffect {
    id: root

    property real radius: 0.0
    property real fillLevel: 0.0
    property real waveAmp: 0.0
    property vector2d itemSize: Qt.vector2d(width, height)
    property real phase: 0.0
    property real bodyAlpha: 0.5
    property color color1: ThemeBackend.primary
    property color color2: color1

    fragmentShader: "file:///" + Caching.serpantinumDir + "/assets/shaders/fluid/liquid_fill.frag.qsb"
}
