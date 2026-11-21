pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects

Item {
    id: root

    property url source: ""
    property url fallback: ""
    property color color: "transparent"
    property bool isMask: false
    property bool roundToIconSize: true
    property bool animated: false

    implicitWidth: 32
    implicitHeight: 32

    property url _effectiveSource: root.source
    onSourceChanged: root._effectiveSource = root.source

    Image {
        id: img
        anchors.fill: parent
        source: root._effectiveSource
        fillMode: Image.PreserveAspectFit
        sourceSize: root.width > 0 && root.height > 0 ? Qt.size(Math.ceil(root.width / 16) * 16, Math.ceil(root.height / 16) * 16) : Qt.size(0, 0)
        smooth: true
        asynchronous: true
        onStatusChanged: {
            if (status === Image.Error && String(root.fallback) !== "" && source !== root.fallback) {
                root._effectiveSource = root.fallback;
            }
        }
        layer.enabled: root.isMask
        layer.effect: MultiEffect {
            colorization: 1.0
            colorizationColor: root.color
        }
    }
}
