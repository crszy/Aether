import QtQuick
import Quickshell.Widgets
import qs.dms.DCommon.Common
import "Carousel.js" as Carousel

Item {
    id: root

    default property alias content: content.data
    property var tooltipText: null
    readonly property DCarousel carousel: ListView.view as DCarousel
    readonly property var geometry: carousel?.keylines ? Carousel.itemGeometry(carousel.keylines, x + width / 2 - carousel.contentX) : ({
            center: 0,
            size: 0
        })
    readonly property real maskWidth: geometry.size

    signal clicked

    width: carousel?.itemWidth ?? 0
    height: carousel?.height ?? 0

    ClippingRectangle {
        id: mask

        x: root.geometry.center - root.maskWidth / 2 - (root.x - (root.carousel?.contentX ?? 0))
        width: root.maskWidth
        height: parent.height
        radius: Math.min(root.carousel?.itemRadius ?? 0, width / 2)
        color: Style.chipSurface
        visible: width > 0

        Item {
            id: content
            x: (mask.width - root.width) / 2
            width: root.width
            height: root.height
        }

        StateLayer {
            tooltipText: root.tooltipText
            onClicked: root.clicked()
        }
    }
}
