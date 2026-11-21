import QtQuick
import qs.dms.DCommon.Common

Item {
    id: root

    property string hours: "00"
    property string minutes: "00"
    property string seconds: ""
    property string dateText: ""
    property bool stacked: false
    property color color: Style.primary
    property color minutesColor: color
    property color secondsColor: minutesColor
    property color supportingColor: Style.onSurfaceVariant
    property string fontFamily: ""
    property int weight: 0
    property bool rounded: true

    readonly property bool variableFont: fontFamily === "" || fontFamily === Style.defaultFontFamily
    readonly property int digitWeight: weight > 0 ? weight : 750
    readonly property real referenceSize: 100
    readonly property string digitFamily: fontFamily || Style.fontFor("ui")
    readonly property int staticWeight: Math.min(Font.Black, digitWeight)

    readonly property bool tall: height > Style.buttonHeightM * 2
    readonly property bool vertical: tall && (stacked || height > width * 0.6)
    readonly property bool inlineSeconds: seconds !== "" && !vertical && width / fullAdvance >= Style.fontSizeDisplay
    readonly property string timeText: hours + ":" + minutes + (inlineSeconds ? ":" + seconds : "")
    readonly property string supportText: [dateText, inlineSeconds ? "" : seconds].filter(s => s !== "").join(" · ")
    readonly property bool hasSupport: supportText !== "" && height - support.height - supportGap >= Style.fontSizeLarge

    readonly property real opticalSize: vertical ? height / 2 : height
    readonly property var digitAxes: variableFont ? {
        "ROND": rounded ? 100 : 20,
        "wght": digitWeight,
        "opsz": opticalSize
    } : {}
    readonly property font metricsFont: Qt.font({
        "family": digitFamily,
        "pixelSize": referenceSize,
        "weight": variableFont ? Font.Normal : staticWeight,
        "variableAxes": digitAxes
    })
    readonly property string widestDigit: Fonts.widestDigit(metricsFont)
    readonly property rect digitInk: digitMetrics.tightBoundingRect
    readonly property real inkRatio: Math.max(0.01, digitInk.height / referenceSize)
    readonly property real rowAdvance: Math.max(0.01, rowMetrics.advanceWidth / referenceSize)
    readonly property real fullAdvance: Math.max(0.01, fullMetrics.advanceWidth / referenceSize)
    readonly property real widthSize: width / rowAdvance
    readonly property real estimateSize: Math.min(widthSize, (vertical ? height / 2 : height) / inkRatio)
    readonly property real supportSize: Math.max(Style.fontSizeMedium, estimateSize * inkRatio * 0.25)
    readonly property real supportGap: supportSize * 0.3
    readonly property real supportHeight: hasSupport ? support.height + supportGap : 0
    readonly property real digitsHeight: height - supportHeight
    readonly property real rowGap: vertical ? digitsHeight * 0.06 : 0
    readonly property real rowHeight: vertical ? (digitsHeight - rowGap) / 2 : digitsHeight
    readonly property real digitSize: Math.max(1, Math.min(widthSize, rowHeight / inkRatio))
    readonly property real cellWidth: digitSize * cellMetrics.advanceWidth / referenceSize
    readonly property real inkHeight: digitSize * inkRatio
    readonly property real blockHeight: inkHeight * (vertical ? 2 : 1) + rowGap + supportHeight
    readonly property real blockTop: (height - blockHeight) / 2

    implicitWidth: Style.fontSizeDisplay * 6
    implicitHeight: Style.fontSizeDisplay * 4

    function inkTop(label) {
        return label.baselineOffset + digitInk.y * digitSize / referenceSize;
    }

    TextMetrics {
        id: digitMetrics
        font: root.metricsFont
        text: "0123456789"
    }

    TextMetrics {
        id: cellMetrics
        font: root.metricsFont
        text: root.widestDigit
    }

    TextMetrics {
        id: fullMetrics
        font: root.metricsFont
        text: (root.hours + ":" + root.minutes + ":" + root.seconds).replace(/\d/g, root.widestDigit)
    }

    TextMetrics {
        id: rowMetrics
        font: root.metricsFont
        text: (root.vertical ? (root.hours.length > root.minutes.length ? root.hours : root.minutes) : root.timeText).replace(/\d/g, root.widestDigit)
    }

    Row {
        visible: !root.vertical
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.blockTop - root.inkTop(colon)
        LayoutMirroring.enabled: false

        DigitCells {
            text: root.hours
        }

        Digits {
            id: colon
            text: ":"
        }

        DigitCells {
            text: root.minutes
            color: root.minutesColor
        }

        Digits {
            visible: root.inlineSeconds
            text: ":"
            color: root.secondsColor
        }

        DigitCells {
            visible: root.inlineSeconds
            text: root.seconds
            color: root.secondsColor
        }
    }

    DigitCells {
        visible: root.vertical
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.blockTop - root.inkTop(colon)
        text: root.hours
    }

    DigitCells {
        visible: root.vertical
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.blockTop + root.inkHeight + root.rowGap - root.inkTop(colon)
        text: root.minutes
        color: root.minutesColor
    }

    StyledText {
        id: support
        visible: root.hasSupport
        y: root.blockTop + root.blockHeight - height
        width: root.width
        text: root.supportText
        color: root.supportingColor
        font.pixelSize: root.supportSize
        font.weight: Style.fontWeightMedium
        minimumPixelSize: Style.fontSizeSmall
        fontSizeMode: Text.HorizontalFit
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignTop
        wrapMode: Text.NoWrap
        elide: Text.ElideRight
    }

    component Digits: StyledText {
        color: root.color
        font.pixelSize: root.digitSize
        font.family: root.digitFamily
        // only Normal resolves to the variable face, other weights pick axis-less static instances
        font.weight: root.variableFont ? Font.Normal : root.staticWeight
        font.variableAxes: root.digitAxes
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignTop
        wrapMode: Text.NoWrap
        elide: Text.ElideNone
        LayoutMirroring.enabled: false
    }

    component DigitCells: Row {
        id: cells

        property string text
        property color color: root.color

        LayoutMirroring.enabled: false

        Repeater {
            model: cells.text.length

            Digits {
                required property int index
                width: root.cellWidth
                text: cells.text.charAt(index)
                color: cells.color
            }
        }
    }
}
