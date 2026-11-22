pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import Quickshell.Widgets
import qs.dms.DCommon.Common

Item {
    id: root

    required property string source
    property int size: 24
    property string cornerIcon: ""
    property int cornerIconSize: Math.max(10, size * 0.4)
    property color cornerIconColor: Style.surfaceText
    property color cornerIconBackground: Style.surface
    property color colorOverride: "transparent"
    property real brightnessOverride: 0.0
    property real contrastOverride: 0.0
    property real saturationOverride: 0.0
    property bool asynchronous: true

    readonly property int rasterSize: Style.iconRasterSize(size) * 2
    readonly property bool hasCornerIcon: cornerIcon !== ""
    readonly property bool hasColorOverride: colorOverride.a > 0
    readonly property bool hasColorEffect: hasColorOverride || brightnessOverride !== 0.0 || contrastOverride !== 0.0 || saturationOverride !== 0.0
    readonly property string resolvedSource: {
        if (!source)
            return "";
        if (source.startsWith("file:///"))
            return source;
        if (source.startsWith("/"))
            return "file:///" + source;
        if (source.startsWith("qrc:"))
            return source;
        return source;
    }

    implicitWidth: size
    implicitHeight: size

    IconImage {
        id: iconImage
        anchors.fill: parent
        source: root.resolvedSource
        smooth: true
        mipmap: true
        asynchronous: root.asynchronous
        implicitSize: root.size * 2
        backer.sourceSize.width: root.rasterSize
        backer.sourceSize.height: root.rasterSize
        backer.cache: true
        layer.enabled: root.hasColorEffect
        layer.smooth: true
        layer.mipmap: true
        layer.effect: MultiEffect {
            saturation: root.saturationOverride
            colorization: root.hasColorOverride ? 1 : 0
            colorizationColor: root.colorOverride
            brightness: root.brightnessOverride
            contrast: root.contrastOverride
        }
    }

    Loader {
        active: root.hasCornerIcon
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: -2
        anchors.bottomMargin: -2

        sourceComponent: Rectangle {
            width: root.cornerIconSize + 4
            height: root.cornerIconSize + 4
            radius: width / 2
            color: root.cornerIconBackground
            border.width: 1
            border.color: Style.surfaceLight

            DIcon {
                anchors.centerIn: parent
                name: root.cornerIcon
                size: root.cornerIconSize
                color: root.cornerIconColor
            }
        }
    }
}
