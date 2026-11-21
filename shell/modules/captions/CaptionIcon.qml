// The app's own icon (from its .exe, via the shell's icon provider) for a title bar. `byDefault` is whether the style
// shows one itself; Settings > Mix & match > "App icon in title bars" can force it on or off.
import QtQuick
import qs.services

Image {
    id: root

    required property var skin
    property bool byDefault: false
    property real size: 16

    readonly property bool wanted: WinSettings.titleBarIcon === "on" || (WinSettings.titleBarIcon !== "off" && byDefault)

    visible: wanted && skin.exePath !== "" && status === Image.Ready
    width: visible ? size : 0
    height: size
    source: wanted && skin.exePath !== "" ? "image://icon/" + skin.exePath : ""
    sourceSize.width: 32
    sourceSize.height: 32
    fillMode: Image.PreserveAspectFit
    asynchronous: true
    smooth: true
    mipmap: true
}
