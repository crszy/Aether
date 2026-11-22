pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common

Item {
    id: root

    required property string iconValue
    required property int iconSize
    property string fallbackText: "A"
    property color iconColor: Style.surfaceText
    property color colorOverride: "transparent"
    property real brightnessOverride: 0.0
    property real contrastOverride: 0.0
    property real saturationOverride: 0.0
    property color fallbackBackgroundColor: Style.surfaceLight
    property real fallbackRadius: Style.cornerRadius
    property color fallbackTextColor: Style.primary
    property real materialIconSizeAdjustment: Style.spacingM
    property real unicodeIconScale: 0.7
    property real fallbackTextScale: 0.4
    property real iconMargins: 0
    property real fallbackLeftMargin: 0
    property real fallbackRightMargin: 0
    property real fallbackTopMargin: 0
    property real fallbackBottomMargin: 0
    property bool animate: false

    readonly property bool isMaterial: iconValue && iconValue.startsWith("material:")
    readonly property bool isUnicode: iconValue && iconValue.startsWith("unicode:")
    readonly property bool isSvgCorner: iconValue && iconValue.startsWith("svg+corner:")
    readonly property bool isSvg: iconValue && !isSvgCorner && iconValue.startsWith("svg:")
    readonly property bool isImage: iconValue && iconValue.startsWith("image:")
    readonly property bool hasColorOverride: colorOverride.a > 0
    readonly property string materialName: isMaterial ? iconValue.substring(9) : ""
    readonly property string unicodeChar: isUnicode ? iconValue.substring(8) : ""
    readonly property string imagePath: isImage ? iconValue.substring(6) : ""
    readonly property string svgSource: {
        if (isSvgCorner) {
            const parts = iconValue.substring(11).split("|");
            return parts[0] || "";
        }
        if (isSvg)
            return iconValue.substring(4);
        return "";
    }
    readonly property string svgCornerIcon: isSvgCorner ? (iconValue.substring(11).split("|")[1] || "") : ""
    readonly property bool hasSpecialPrefix: isMaterial || isUnicode || isSvg || isSvgCorner || isImage
    readonly property string iconPath: {
        if (hasSpecialPrefix || !iconValue)
            return "";
        return Paths.resolveIconPath(iconValue);
    }
    readonly property int iconRequestSize: Style.iconRasterSize(iconSize)
    readonly property string poolKey: iconPath ? `${iconPath}|${iconRequestSize}|${Screen.name}` : ""
    readonly property int iconStatus: _icon?.status ?? Image.Null
    property Item _icon: null
    property Item _outgoing: null
    property bool _ready: false

    visible: iconValue !== undefined && iconValue !== ""

    onPoolKeyChanged: _swapIcon()
    onIconStatusChanged: {
        if (iconStatus !== Image.Ready || !_outgoing)
            return;
        IconImagePool.release(_outgoing);
        _outgoing = null;
    }

    Component.onCompleted: {
        _ready = true;
        _swapIcon();
    }

    Component.onDestruction: {
        if (_outgoing)
            IconImagePool.release(_outgoing);
        if (_icon)
            IconImagePool.release(_icon);
    }

    function _swapIcon() {
        if (!_ready || (_icon?.poolKey ?? "") === poolKey)
            return;
        if (_outgoing) {
            IconImagePool.release(_outgoing);
            _outgoing = null;
        }
        const next = poolKey ? IconImagePool.acquire(iconSlot, poolKey, iconPath, iconRequestSize, Screen.devicePixelRatio) : null;
        if (_icon && next && next.status !== Image.Ready)
            _outgoing = _icon;
        else if (_icon)
            IconImagePool.release(_icon);
        _icon = next;
    }

    // Load only the face in use; a hidden empty Text slows every move of its parents.
    Loader {
        anchors.centerIn: parent
        sourceComponent: root.isMaterial ? materialFace : root.isUnicode ? unicodeFace : root.isSvg || root.isSvgCorner ? svgFace : null
    }

    Component {
        id: materialFace

        DIcon {
            name: root.materialName
            size: root.iconSize - root.materialIconSizeAdjustment
            color: root.hasColorOverride ? root.colorOverride : root.iconColor
        }
    }

    Component {
        id: unicodeFace

        StyledText {
            text: root.unicodeChar
            font.pixelSize: root.iconSize * root.unicodeIconScale
            color: root.hasColorOverride ? root.colorOverride : root.iconColor
        }
    }

    Component {
        id: svgFace

        DSVGIcon {
            source: root.svgSource
            size: root.iconSize
            cornerIcon: root.svgCornerIcon
            colorOverride: root.colorOverride
            brightnessOverride: root.brightnessOverride
            contrastOverride: root.contrastOverride
            saturationOverride: root.saturationOverride
        }
    }

    CachingImage {
        id: cachingImg
        anchors.fill: parent
        imagePath: root.imagePath
        maxCacheSize: root.iconRequestSize * 2
        animate: root.animate
        visible: root.isImage && status === Image.Ready
    }

    Item {
        id: iconSlot
        anchors.fill: parent
        anchors.margins: root.iconMargins
    }

    Rectangle {
        id: fallbackRect

        anchors.fill: parent
        anchors.leftMargin: root.fallbackLeftMargin
        anchors.rightMargin: root.fallbackRightMargin
        anchors.topMargin: root.fallbackTopMargin
        anchors.bottomMargin: root.fallbackBottomMargin
        visible: !root.hasSpecialPrefix && (root.poolKey === "" || root.iconStatus === Image.Error)
        color: root.fallbackBackgroundColor
        radius: root.fallbackRadius
        border.width: 0
        border.color: Style.primarySelected

        StyledText {
            anchors.centerIn: parent
            text: root.fallbackText
            font.pixelSize: root.iconSize * root.fallbackTextScale
            color: root.fallbackTextColor
            font.weight: Style.fontWeightMedium
        }
    }
}
