import QtQuick
import QtQuick.Shapes
import qs.dms.DCommon.Common

Item {
    id: root

    property real size: 48
    property real strokeWidth: Math.min(4, Math.max(2, size / 10))
    property color color: Style.primary
    property bool running: visible

    implicitWidth: size
    implicitHeight: size

    onRunningChanged: {
        if (running)
            return;
        rotator.globalRotation = 0;
        rotator.stepRotation = 0;
        arc.progress = 0.1;
    }

    Item {
        id: rotator

        property real globalRotation: 0
        property real stepRotation: 0

        anchors.fill: parent
        rotation: globalRotation + stepRotation

        NumberAnimation on globalRotation {
            from: 0
            to: 1080
            duration: 6000
            loops: Animation.Infinite
            running: root.running
        }

        SequentialAnimation on stepRotation {
            loops: Animation.Infinite
            running: root.running

            NumberAnimation {
                from: 0
                to: 90
                duration: 300
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Anims.emphasizedDecel
            }
            PauseAnimation {
                duration: 1200
            }
            NumberAnimation {
                from: 90
                to: 180
                duration: 300
            }
            PauseAnimation {
                duration: 1200
            }
            NumberAnimation {
                from: 180
                to: 270
                duration: 300
            }
            PauseAnimation {
                duration: 1200
            }
            NumberAnimation {
                from: 270
                to: 360
                duration: 300
            }
            PauseAnimation {
                duration: 1200
            }
        }

        Shape {
            id: arc

            property real progress: 0.1

            anchors.fill: parent
            antialiasing: true
            preferredRendererType: Shape.CurveRenderer

            SequentialAnimation on progress {
                loops: Animation.Infinite
                running: root.running

                NumberAnimation {
                    from: 0.1
                    to: 0.87
                    duration: 3000
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Anims.standard
                }
                NumberAnimation {
                    from: 0.87
                    to: 0.1
                    duration: 3000
                }
            }

            ShapePath {
                strokeColor: root.color
                strokeWidth: root.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap

                PathAngleArc {
                    centerX: arc.width / 2
                    centerY: arc.height / 2
                    radiusX: Math.max(1, (Math.min(arc.width, arc.height) - root.strokeWidth) / 2)
                    radiusY: radiusX
                    startAngle: 0
                    sweepAngle: arc.progress * 360
                }
            }
        }
    }
}
