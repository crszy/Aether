// Windows (Aether): Ryoku's Stage depth scene is a Qt Quick3D mesh, and Aether's Qt has no Quick3D. This keeps the
// same properties and shows the wallpaper flat (no depth / parallax), so the Stage and desktop load.
import QtQuick

Item {
    id: root

    property url backgroundPath
    property url depthPath
    property url mattePath
    property size textureSize: Qt.size(Math.max(1, width), Math.max(1, height))
    property real pointerX: 0
    property real pointerY: 0
    property string mode: "spatial"
    property bool stretch: false
    property string screenName: ""
    property bool layered: false

    function publishPivot() {}
    function updateCamera() {}

    Image {
        anchors.fill: parent
        source: root.backgroundPath
        fillMode: root.stretch ? Image.Stretch : Image.PreserveAspectCrop
        asynchronous: true
        cache: false
    }
}
