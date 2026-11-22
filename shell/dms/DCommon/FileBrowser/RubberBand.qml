pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common

Rectangle {
    id: band

    property real originX: 0
    property real originY: 0
    property real pointX: 0
    property real pointY: 0

    readonly property bool past: Math.abs(pointX - originX) > FileBrowserMetrics.rubberBandMinimum || Math.abs(pointY - originY) > FileBrowserMetrics.rubberBandMinimum

    x: Math.min(originX, pointX)
    y: Math.min(originY, pointY)
    width: Math.abs(pointX - originX)
    height: Math.abs(pointY - originY)
    radius: Style.cornerRadiusXS
    color: Style.withAlpha(Style.primary, Style.stateLayerHover)
    border.width: Style.outlineWidth
    border.color: Style.primary
}
