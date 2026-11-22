// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick
import QtQuick.Effects

Item {
    id: ri
    property string source: ""
    property real cornerRadius: width / 2
    property string fallback: ""

    Image {
        id: img
        anchors.fill: parent
        source: ri.source
        fillMode: Image.PreserveAspectCrop
        visible: false
        asynchronous: true
        cache: false
        sourceSize.width: 256
        sourceSize.height: 256
    }
    Item {
        id: msk
        anchors.fill: parent
        visible: false
        layer.enabled: true
        Rectangle { anchors.fill: parent; radius: ri.cornerRadius }
    }
    MultiEffect {
        anchors.fill: parent
        source: img
        maskEnabled: true
        maskSource: msk
        visible: img.status === Image.Ready
    }
    Rectangle {
        anchors.fill: parent
        radius: ri.cornerRadius
        color: "#202022"
        visible: img.status !== Image.Ready
        T { anchors.centerIn: parent; text: ri.fallback; font.pixelSize: parent.width * 0.42; color: "#8a8a8a" }
    }
}
