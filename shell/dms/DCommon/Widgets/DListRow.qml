import QtQuick
import QtQuick.Templates as T
import qs.dms.DCommon.Common

T.Control {
    id: root

    property bool firstInGroup: true
    property bool lastInGroup: true
    readonly property color contentColor: enabled ? Style.onSurface : Style.onSurface_38
    readonly property color supportingContentColor: enabled ? Style.onSurfaceVariant : Style.onSurface_38

    implicitHeight: Style.listItemHeight
    focusPolicy: Qt.NoFocus
    hoverEnabled: false
    Accessible.role: Accessible.ListItem
    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    background: Rectangle {
        color: Style.foregroundColor(Style.cardSurface, Style.isFloatingWindow(root))
        border.width: Style.layerOutlineWidth
        border.color: Style.outlineMedium
        topLeftRadius: root.firstInGroup ? Style.groupedListOuterRadius : Style.groupedListInnerRadius
        topRightRadius: topLeftRadius
        bottomLeftRadius: root.lastInGroup ? Style.groupedListOuterRadius : Style.groupedListInnerRadius
        bottomRightRadius: bottomLeftRadius
    }
}
