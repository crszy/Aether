import "MaterialShapes.js" as Shapes
import QtQuick
import QtQuick.Shapes
import qs.dms.DCommon.Common

Item {
    id: root

    property string shape: "cookie4"
    property color color: Style.primary
    property color trackColor: "transparent"
    property real fillProgress: 1
    property bool respectThemeShape: true

    readonly property var catalog: Shapes.catalog
    readonly property real rotationScale: Shapes.rotationScale(shape)
    readonly property string pathData: Shapes.buildPath(shape, width, height, respectThemeShape && Style.shapeScale === 0)
    readonly property LinearGradient progressGradient: LinearGradient {
        x1: 0
        y1: 0
        x2: 0
        y2: root.height
        GradientStop {
            position: 0
            color: root.trackColor
        }
        GradientStop {
            position: 1 - Math.max(0, Math.min(1, root.fillProgress))
            color: root.trackColor
        }
        GradientStop {
            position: 1 - Math.max(0, Math.min(1, root.fillProgress))
            color: root.color
        }
        GradientStop {
            position: 1
            color: root.color
        }
    }

    implicitWidth: Style.iconButtonSize
    implicitHeight: Style.iconButtonSize

    function rotationScaleForAspectRatio(aspectRatio) {
        return Shapes.rotationScale(shape, aspectRatio);
    }

    function buildPath(kind, w, h) {
        return Shapes.buildPath(kind, w, h, respectThemeShape && Style.shapeScale === 0);
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "transparent"
            strokeWidth: 0
            fillColor: root.color

            fillGradient: root.fillProgress >= 1 ? null : root.progressGradient

            PathSvg {
                path: root.pathData
            }
        }
    }
}
