pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import qs.dms.DCommon.Common

Item {
    id: root

    property var values: []
    property var secondaryValues: []
    property var xValues: []
    property real minimumX: 0
    property real maximumX: 1
    property real maximum: 0
    property real minimum: 0
    property bool autoRange: false
    property real rangePadding: 0.15
    property int historyLength: 0
    property color lineColor: Style.primary
    property color secondaryLineColor: Style.tertiary
    property real lineWidth: Style.outlineWidthFocused
    property real fillOpacity: Style.tonalTintAlpha
    property bool curved: true
    property bool showDots: false
    property real dotRadius: 3
    property real insetTop: lineWidth
    property real insetBottom: lineWidth
    property real edgeExtension: 0

    readonly property var range: {
        const series = [values ?? [], secondaryValues ?? []];
        let lo = Infinity;
        let hi = -Infinity;
        for (const points of series) {
            for (const v of points) {
                if (typeof v !== "number" || !isFinite(v))
                    continue;
                lo = Math.min(lo, v);
                hi = Math.max(hi, v);
            }
        }
        if (!isFinite(lo)) {
            lo = 0;
            hi = 1;
        }
        if (!autoRange) {
            lo = minimum;
            hi = maximum > 0 ? maximum : Math.max(hi * (1 + rangePadding), 1);
            return {
                "min": lo,
                "max": hi
            };
        }
        const span = Math.max(hi - lo, 1);
        return {
            "min": lo - span * rangePadding,
            "max": hi + span * rangePadding
        };
    }
    readonly property var points: project(values)
    readonly property var secondaryPoints: project(secondaryValues)
    readonly property string linePath: tracePath(points)
    readonly property string fillPath: fillOpacity > 0 ? closePath(points, linePath) : ""
    readonly property string secondaryLinePath: tracePath(secondaryPoints)

    implicitHeight: Style.iconButtonSize

    function project(series) {
        const list = series ?? [];
        const count = list.length;
        if (count === 0 || width <= 0 || height <= 0)
            return [];
        const slots = Math.max(2, historyLength, count);
        const step = width / (slots - 1);
        const startX = historyLength > 0 ? width - (count - 1) * step : 0;
        const top = insetTop;
        const bottom = Math.max(top, height - insetBottom);
        const span = Math.max(range.max - range.min, 0.000001);
        const out = [];
        for (let i = 0; i < count; i++) {
            const v = list[i];
            if (typeof v !== "number" || !isFinite(v))
                continue;
            const explicitX = xValues.length > 0;
            const x = explicitX ? xValues[i] : i;
            if (typeof x !== "number" || !isFinite(x))
                continue;
            const t = Math.max(0, Math.min(1, (v - range.min) / span));
            out.push({
                "x": explicitX ? width * Math.max(0, Math.min(1, (x - minimumX) / Math.max(maximumX - minimumX, 0.000001))) : startX + i * step,
                "y": bottom - t * (bottom - top),
                "value": v
            });
        }
        return out;
    }

    function extendPoint(point, neighbor, distance) {
        const span = point.x - neighbor.x;
        const y = span === 0 ? point.y : point.y + distance * (point.y - neighbor.y) / span;
        return {
            "x": point.x + distance,
            "y": Math.max(insetTop, Math.min(height - insetBottom, y))
        };
    }

    function tracePath(pts) {
        if (pts.length === 0)
            return "";
        const f = v => v.toFixed(2);
        const extension = Math.max(0, edgeExtension);
        const first = extendPoint(pts[0], pts[Math.min(1, pts.length - 1)], -extension);
        const last = extendPoint(pts[pts.length - 1], pts[Math.max(0, pts.length - 2)], extension);
        const tail = extension > 0 ? " L " + f(last.x) + " " + f(last.y) : "";
        let d = "M " + f(first.x) + " " + f(first.y);
        if (extension > 0)
            d += " L " + f(pts[0].x) + " " + f(pts[0].y);
        if (pts.length === 1)
            return d + tail;
        if (!curved) {
            for (let i = 1; i < pts.length; i++)
                d += " L " + f(pts[i].x) + " " + f(pts[i].y);
            return d + tail;
        }
        for (let i = 0; i < pts.length - 1; i++) {
            const p0 = pts[Math.max(0, i - 1)];
            const p1 = pts[i];
            const p2 = pts[i + 1];
            const p3 = pts[Math.min(pts.length - 1, i + 2)];
            d += " C " + f(p1.x + (p2.x - p0.x) / 6) + " " + f(p1.y + (p2.y - p0.y) / 6) + " " + f(p2.x - (p3.x - p1.x) / 6) + " " + f(p2.y - (p3.y - p1.y) / 6) + " " + f(p2.x) + " " + f(p2.y);
        }
        return d + tail;
    }

    function closePath(pts, line) {
        if (pts.length < 2)
            return "";
        const f = v => v.toFixed(2);
        const bottom = f(height);
        const extension = Math.max(0, edgeExtension);
        return line + " L " + f(pts[pts.length - 1].x + extension) + " " + bottom + " L " + f(pts[0].x - extension) + " " + bottom + " Z";
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "transparent"
            strokeWidth: 0
            fillColor: Style.withAlpha(root.lineColor, root.fillOpacity)

            PathSvg {
                path: root.fillPath
            }
        }

        ShapePath {
            strokeColor: root.secondaryLineColor
            strokeWidth: root.lineWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg {
                path: root.secondaryLinePath
            }
        }

        ShapePath {
            strokeColor: root.lineColor
            strokeWidth: root.lineWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg {
                path: root.linePath
            }
        }
    }

    Repeater {
        model: root.showDots ? root.points : []

        Rectangle {
            required property var modelData

            x: modelData.x - root.dotRadius
            y: modelData.y - root.dotRadius
            width: root.dotRadius * 2
            height: width
            radius: root.dotRadius
            color: root.lineColor
        }
    }
}
