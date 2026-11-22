pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import QtQuick.Window
import qs.dms.DCommon.Common
import "../Common/Hct.js" as Hct

Item {
    id: root

    property string hours: "00"
    property string minutes: "00"
    property color color: Style.primary
    property color shadowColor: {
        const source = Hct.toHct(color);
        return Hct.fromHct(source.hue, source.chroma, source.tone > 50 ? 30 : 80);
    }
    property string fontFamily: ""
    property int weight: 1000
    property real digitSize: Style.fontSizeDisplayLarge * 2
    property real outlineWidth: digitSize * 0.025
    property real renderScale: 1

    readonly property real tracking: -digitSize * 0.22
    readonly property real rowAdvance: digitSize * 0.7
    readonly property bool variableFont: fontFamily === "" || fontFamily === Style.defaultFontFamily
    readonly property real padding: outlineWidth + Style.spacingXS
    readonly property real pixelRatio: Window.window?.devicePixelRatio ?? Screen.devicePixelRatio
    readonly property real supersample: 4
    readonly property real rasterScale: Math.max(1, renderScale * pixelRatio * supersample)
    readonly property font digitFont: Qt.font({
        family: fontFamily || Style.defaultFontFamily,
        pixelSize: digitSize,
        weight: variableFont ? Font.Normal : Math.min(Font.Black, weight),
        variableAxes: variableFont ? {
            "wght": weight,
            "ROND": 20,
            "opsz": 18
        } : {}
    })
    // reading the font registers the dependency, the method calls alone bind nothing
    readonly property real cellWidth: {
        const font = digitMetrics.font;
        return Math.max(..."0123456789".split("").map(digit => digitMetrics.advanceWidth(digit)));
    }
    readonly property rect digitInk: {
        const font = digitMetrics.font;
        return digitMetrics.tightBoundingRect("0123456789");
    }

    function cellX(index, ink) {
        return index * (cellWidth + tracking) + (cellWidth - ink.width) / 2 - ink.x;
    }

    FontMetrics {
        id: digitMetrics
        font: root.digitFont
    }

    implicitWidth: Math.max(hoursRow.width, minutesRow.width) + padding * 2
    implicitHeight: Math.max(hoursRow.height, minutesRow.y - hoursRow.y + minutesRow.height) + padding * 2

    DigitRow {
        id: hoursRow
        x: (root.width - root.implicitWidth) / 2 + root.padding
        y: root.padding
        text: root.hours.padStart(2, "0")
    }

    DigitRow {
        id: minutesRow
        x: hoursRow.x
        y: hoursRow.y + hoursRow.baselineOffset + root.rowAdvance - baselineOffset
        text: root.minutes.padStart(2, "0")
    }

    component GlyphSource: ShaderEffectSource {
        required property DigitRow row
        required property ClockDigit digit
        readonly property vector4d faceRect: Qt.vector4d((row.x + digit.x + sourceRect.x) / root.width, (row.y + digit.y + sourceRect.y) / root.height, sourceRect.width / root.width, sourceRect.height / root.height)

        sourceItem: digit
        sourceRect: digit.captureRect
        hideSource: true
        smooth: true
        mipmap: true
        textureSize: Qt.size(Math.ceil(sourceRect.width * root.rasterScale), Math.ceil(sourceRect.height * root.rasterScale))
    }

    GlyphSource {
        id: hoursFirst
        row: hoursRow
        digit: hoursRow.first
    }

    GlyphSource {
        id: hoursSecond
        row: hoursRow
        digit: hoursRow.second
    }

    GlyphSource {
        id: minutesFirst
        row: minutesRow
        digit: minutesRow.first
    }

    GlyphSource {
        id: minutesSecond
        row: minutesRow
        digit: minutesRow.second
    }

    ShaderEffect {
        anchors.fill: parent
        layer.enabled: true
        layer.smooth: true
        layer.mipmap: true
        layer.textureSize: Qt.size(Math.ceil(width * root.rasterScale / 2), Math.ceil(height * root.rasterScale / 2))
        readonly property var hoursFirst: hoursFirst
        readonly property var hoursSecond: hoursSecond
        readonly property var minutesFirst: minutesFirst
        readonly property var minutesSecond: minutesSecond
        readonly property vector4d hoursFirstRect: hoursFirst.faceRect
        readonly property vector4d hoursSecondRect: hoursSecond.faceRect
        readonly property vector4d minutesFirstRect: minutesFirst.faceRect
        readonly property vector4d minutesSecondRect: minutesSecond.faceRect
        readonly property color lightColor: root.color
        readonly property color darkColor: root.shadowColor
        fragmentShader: Qt.resolvedUrl("../Shaders/qsb/clock_knockout.frag.qsb")
    }

    component ClockDigit: Shape {
        id: digit

        required property string text
        readonly property rect inkBounds: metrics.tightBoundingRect
        readonly property real capturePadding: 2 / root.rasterScale
        readonly property rect captureRect: Qt.rect(boundingRect.x - capturePadding, boundingRect.y - capturePadding, boundingRect.width + capturePadding * 2, boundingRect.height + capturePadding * 2)
        readonly property color fillChannel: Qt.rgba(1, 0, 0, 1)
        readonly property color outlineChannel: Qt.rgba(0, 0, 0, 1)

        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillRule: ShapePath.WindingFill
            fillColor: digit.outlineChannel
            strokeColor: digit.outlineChannel
            strokeWidth: root.outlineWidth * 2
            joinStyle: ShapePath.RoundJoin

            PathText {
                y: digit.inkBounds.y
                text: digit.text
                font: root.digitFont
            }
        }

        ShapePath {
            fillRule: ShapePath.WindingFill
            fillColor: digit.fillChannel
            strokeWidth: -1

            PathText {
                y: digit.inkBounds.y
                text: digit.text
                font: root.digitFont
            }
        }

        TextMetrics {
            id: metrics
            font: root.digitFont
            text: digit.text
            renderType: Text.CurveRendering
        }
    }

    component DigitRow: Item {
        id: row

        required property string text
        readonly property alias first: firstDigit
        readonly property alias second: secondDigit

        implicitWidth: root.cellWidth * 2 + root.tracking
        implicitHeight: root.digitInk.height
        baselineOffset: -root.digitInk.y

        ClockDigit {
            id: firstDigit
            x: root.cellX(0, inkBounds)
            y: row.baselineOffset
            text: row.text[0] ?? ""
        }

        ClockDigit {
            id: secondDigit
            x: root.cellX(1, inkBounds)
            y: row.baselineOffset
            text: row.text[1] ?? ""
        }
    }
}
