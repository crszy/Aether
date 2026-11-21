// TokyoRGB - on top of Aether Settings: an RGB border that keeps turning, with a soft glow, like a gaming
// keyboard. Clicks pass through (no `interactive: true` here). Faster or slower: change `duration`.
import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import qs.services

Item {
    id: root

    property var surface
    property bool active: true

    property real angle: 0
    readonly property real corner: AetherStyle.cornerXL

    NumberAnimation on angle {
        from: 0
        to: 360
        duration: 4000
        loops: Animation.Infinite
        running: root.active
    }

    // the rainbow, turning
    Item {
        id: rainbow

        anchors.fill: parent
        visible: false
        layer.enabled: true

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeWidth: 0
                strokeColor: "transparent"
                fillGradient: ConicalGradient {
                    centerX: root.width / 2
                    centerY: root.height / 2
                    angle: root.angle
                    GradientStop { position: 0.0; color: "#ff2bd6" }
                    GradientStop { position: 0.17; color: "#ff3b3b" }
                    GradientStop { position: 0.33; color: "#ffd23b" }
                    GradientStop { position: 0.5; color: "#3bff8a" }
                    GradientStop { position: 0.67; color: "#00e5ff" }
                    GradientStop { position: 0.83; color: "#7b2bff" }
                    GradientStop { position: 1.0; color: "#ff2bd6" }
                }
                startX: 0
                startY: 0
                PathLine { x: root.width; y: 0 }
                PathLine { x: root.width; y: root.height }
                PathLine { x: 0; y: root.height }
                PathLine { x: 0; y: 0 }
            }
        }
    }

    // only the edge of it shows: a ring the shape of the window
    Item {
        id: ring

        anchors.fill: parent
        visible: false
        layer.enabled: true

        Rectangle {
            anchors.fill: parent
            radius: root.corner
            color: "transparent"
            border.width: 4
            border.color: "white"
        }
    }

    Item {
        id: glowRing

        anchors.fill: parent
        visible: false
        layer.enabled: true

        Rectangle {
            anchors.fill: parent
            radius: root.corner
            color: "transparent"
            border.width: 26
            border.color: "white"
        }
    }

    // soft glow inside the edge
    MultiEffect {
        anchors.fill: parent
        source: rainbow
        maskEnabled: true
        maskSource: glowRing
        blurEnabled: true
        blur: 1
        blurMax: 32
        opacity: 0.8
    }

    // the sharp line
    MultiEffect {
        anchors.fill: parent
        source: rainbow
        maskEnabled: true
        maskSource: ring
    }
}
