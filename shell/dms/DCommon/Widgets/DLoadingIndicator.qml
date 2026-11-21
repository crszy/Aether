import "MaterialShapes.js" as Shapes
import QtQuick
import qs.dms.DCommon.Common

Item {
    id: root

    property real size: 48
    property bool contained: false
    property color containerColor: Style.primaryContainer
    property color color: contained ? Style.onPrimaryContainer : Style.primary
    property bool running: visible

    readonly property var shapes: ["softBurst", "cookie9", "pentagon", "pill", "sunny", "cookie4", "oval"]
    readonly property int samples: 256
    readonly property real activeRatio: 38 / 48
    readonly property int morphInterval: 650
    readonly property int rotationDuration: 4666
    readonly property real dampingRatio: 0.6
    readonly property real stiffness: 200
    readonly property real visibilityThreshold: 0.1
    readonly property var profiles: shapes.map(kind => Shapes.polarProfile(kind, samples))
    readonly property var centers: profiles.map(radii => {
        const bounds = Shapes.polarBounds(radii);
        return Qt.point(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
    })
    readonly property real shapeScale: Shapes.morphScale(profiles) * activeRatio
    readonly property real decay: dampingRatio * Math.sqrt(stiffness)
    readonly property real frequency: Math.sqrt(stiffness * (1 - dampingRatio * dampingRatio))
    readonly property real settleTime: Math.log(Math.hypot(1, decay / frequency) / visibilityThreshold) / decay

    property real elapsed: 0
    readonly property int step: Math.floor(elapsed / morphInterval)
    readonly property real morph: springAt((elapsed - step * morphInterval) / 1000)

    implicitWidth: size
    implicitHeight: size

    onRunningChanged: {
        if (!running)
            elapsed = 0;
    }

    function springAt(seconds) {
        if (seconds >= settleTime)
            return 1;
        return 1 - Math.exp(-decay * seconds) * (Math.cos(frequency * seconds) + decay / frequency * Math.sin(frequency * seconds));
    }

    Canvas {
        id: profileTexture

        width: root.samples
        height: root.shapes.length
        visible: false
        onPaint: {
            const context = getContext("2d");
            root.profiles.forEach((radii, row) => radii.forEach((radius, column) => {
                    const value = Math.round(Math.max(0, Math.min(1, radius)) * 65535);
                    context.fillStyle = Qt.rgba((value >> 8) / 255, (value & 255) / 255, 0, 1);
                    context.fillRect(column, row, 1, 1);
                }));
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(root.width, root.height)
        height: width
        radius: Style.fullRadius(width, height)
        color: root.containerColor
        visible: root.contained
    }

    ShaderEffect {
        readonly property int fromShape: root.step % root.shapes.length
        readonly property int toShape: (root.step + 1) % root.shapes.length
        readonly property point fromCenter: root.centers[fromShape]
        readonly property point toCenter: root.centers[toShape]

        property var profiles: profileTexture
        property color color: root.color
        property point offset: Qt.point((fromCenter.x + (toCenter.x - fromCenter.x) * progress) * unitPx, (fromCenter.y + (toCenter.y - fromCenter.y) * progress) * unitPx)
        property real sizePx: width
        property real unitPx: width * root.shapeScale
        property real fromRow: fromShape
        property real toRow: toShape
        property real progress: Math.max(0, Math.min(1, root.morph))
        property real angle: (root.morph * 90 + (root.step + 1) * 90 + root.elapsed % root.rotationDuration / root.rotationDuration * 360) * Math.PI / 180
        property real rows: root.shapes.length
        property real samples: root.samples

        anchors.centerIn: parent
        width: Math.min(root.width, root.height)
        height: width
        fragmentShader: Qt.resolvedUrl("../Shaders/qsb/loading_indicator.frag.qsb")
    }

    FrameAnimation {
        running: root.running
        onTriggered: root.elapsed += frameTime * 1000
    }
}
