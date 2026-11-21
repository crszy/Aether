import QtQuick
import qs.dms.DCommon.Common

Rectangle {
    property bool firstInGroup: true
    property bool lastInGroup: true

    color: Style.hostSurface
    radius: Style.groupedListInnerRadius
    topLeftRadius: firstInGroup ? Style.groupedListOuterRadius : radius
    topRightRadius: topLeftRadius
    bottomLeftRadius: lastInGroup ? Style.groupedListOuterRadius : radius
    bottomRightRadius: bottomLeftRadius
    Accessible.role: Accessible.ListItem
}
