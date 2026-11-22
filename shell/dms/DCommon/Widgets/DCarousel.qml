pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import "Carousel.js" as Carousel
import "../Common/WheelInput.js" as WheelInput

ListView {
    id: root

    property real preferredItemWidth: height
    property real smallItemWidth: Style.listItemHeight
    property real itemRadius: Style.cornerRadiusXL
    readonly property var arrangement: Carousel.arrange(width, preferredItemWidth, smallItemWidth, spacing)
    readonly property real itemWidth: arrangement?.itemWidth ?? 0
    readonly property var keylines: arrangement ? Carousel.stateAt(arrangement, count, width, contentX - originX) : null

    function scrollBy(wheel) {
        const delta = WheelInput.isTouchpad(wheel) ? WheelInput.dominantDelta(wheel.pixelDelta) : WheelInput.dominantDelta(wheel.angleDelta) / 120 * (itemWidth + spacing);
        const maxX = originX + Math.max(0, contentWidth - width);
        contentX = Math.max(originX, Math.min(maxX, contentX - delta));
    }

    orientation: ListView.Horizontal
    spacing: Style.spacingS
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    snapMode: ListView.SnapOneItem
    displayMarginBeginning: (arrangement?.overhang ?? 0) + itemWidth + spacing
    displayMarginEnd: displayMarginBeginning

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => root.scrollBy(event)
    }
}
