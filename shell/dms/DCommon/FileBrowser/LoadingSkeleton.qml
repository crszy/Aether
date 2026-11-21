pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common

Column {
    id: skeleton

    property real rowHeight: FileBrowserMetrics.listRowHeight
    property int rows: Math.max(1, Math.floor(height / (rowHeight + Style.groupedListGap)))

    spacing: Style.groupedListGap

    SequentialAnimation on opacity {
        running: FileBrowserMetrics.animationsEnabled && skeleton.visible
        loops: Animation.Infinite

        NumberAnimation {
            to: FileBrowserMetrics.skeletonOpacity
            duration: Style.currentAnimationBaseDuration
            easing.type: Style.standardEasing
        }

        NumberAnimation {
            to: 1
            duration: Style.currentAnimationBaseDuration
            easing.type: Style.standardEasing
        }
    }

    Repeater {
        model: skeleton.rows

        Rectangle {
            required property int index

            width: skeleton.width
            height: skeleton.rowHeight
            radius: Style.groupedListInnerRadius
            color: Style.surfaceContainerHigh

            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: Style.spacingM
                anchors.verticalCenter: parent.verticalCenter
                width: Style.iconSize
                height: Style.iconSize
                radius: Style.cornerRadiusXS
                color: Style.surfaceContainerHighest
            }

            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: Style.spacingM * 2 + Style.iconSize
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(parent.width / 2, Style.spacingXL * (6 + index % 4))
                height: Style.fontSizeMedium
                radius: Style.cornerRadiusXS
                color: Style.surfaceContainerHighest
            }
        }
    }
}
