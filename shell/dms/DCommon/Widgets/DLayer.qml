import QtQuick

Item {
    id: root

    property real fallbackDevicePixelRatio: Screen.devicePixelRatio
    readonly property real renderDevicePixelRatio: Window.window?.devicePixelRatio ?? fallbackDevicePixelRatio

    layer.enabled: true
    layer.smooth: false
    layer.textureSize: Qt.size(Math.max(1, Math.round(width * renderDevicePixelRatio)), Math.max(1, Math.round(height * renderDevicePixelRatio)))
}
