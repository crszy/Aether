import QtQuick
import qs.dms.DCommon.Common
import "../Common/Contrast.js" as Contrast

DShapeHero {
    id: root

    property string brand: ""
    property string accent: ""
    property string caption: ""
    property string symbol: ""
    property int atomicNumber: 0
    property string atomicMass: ""

    readonly property string release: accent !== "" ? brand + " " + accent : brand
    readonly property bool hasElement: symbol !== "" && caption.toLowerCase().startsWith(symbol.toLowerCase())
    readonly property string headline: hasElement ? caption.slice(symbol.length) : release
    readonly property string badge: hasElement ? release : caption
    readonly property real lockupBaseSize: Style.fontSizeDisplayLarge * 0.8
    readonly property real lockupScale: Math.min(1, availableWidth / Math.max(1, (hasElement ? lockupBaseSize * 1.6 + Style.spacingXS : 0) + headlineMetrics.width))
    readonly property real lockupSize: lockupBaseSize * lockupScale
    readonly property real tileSize: lockupSize * 1.6
    readonly property color onPrimaryColor: Contrast.readableOn(Style.primary, [Style.onPrimary, Style.surface, Style.surfaceText, Qt.rgba(1, 1, 1, 1), Qt.rgba(0, 0, 0, 1)])

    width: parent?.width ?? 0
    accentSize: tileSize
    paddingV: Style.spacingXL

    StyledTextMetrics {
        id: headlineMetrics
        text: root.headline
        font.pixelSize: root.lockupBaseSize
        font.weight: Style.fontWeightBold
    }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Style.spacingXS
        LayoutMirroring.enabled: false

        Rectangle {
            id: tile
            width: root.tileSize
            height: width
            radius: Style.cornerRadiusS
            color: Style.primary
            visible: root.hasElement

            StyledText {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: tile.width * 0.07
                text: root.atomicNumber
                font.pixelSize: tile.width * 0.15
                font.weight: Style.fontWeightMedium
                color: root.onPrimaryColor
            }

            StyledText {
                id: symbolText
                anchors.right: parent.right
                anchors.rightMargin: tile.width * 0.06
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: tile.width * 0.03
                text: root.symbol
                font.pixelSize: root.lockupSize
                font.weight: Style.fontWeightBold
                color: root.onPrimaryColor
            }

            StyledText {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: tile.width * 0.05
                text: root.atomicMass
                font.pixelSize: tile.width * 0.11
                color: root.onPrimaryColor
            }
        }

        StyledText {
            y: root.hasElement ? symbolText.y + symbolText.baselineOffset - baselineOffset : 0
            text: root.headline
            font.pixelSize: root.lockupSize
            font.weight: Style.fontWeightBold
            color: root.contentColor
        }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width, badgeText.implicitWidth + Style.spacingL * 2)
        height: Style.buttonHeightXXS
        radius: Style.fullRadius(width, height)
        color: Style.primary
        visible: root.badge !== ""

        StyledText {
            id: badgeText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - Style.spacingL * 2)
            text: root.badge
            font.pixelSize: Style.fontSizeMedium
            font.weight: Style.fontWeightBold
            color: root.onPrimaryColor
            elide: Text.ElideRight
        }
    }
}
