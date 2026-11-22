import QtQuick
import QtQuick.Templates as T
import qs.dms.DCommon.Common

T.CheckBox {
    id: root

    property alias color: surface.color
    property alias radius: surface.radius
    property alias border: surface.border
    property alias topLeftRadius: surface.topLeftRadius
    property alias topRightRadius: surface.topRightRadius
    property alias bottomLeftRadius: surface.bottomLeftRadius
    property alias bottomRightRadius: surface.bottomRightRadius

    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    checkable: false
    nextCheckState: () => checkState
    Accessible.role: Accessible.Button
    Accessible.onPressAction: click()
    Accessible.onToggleAction: {
        if (checkable)
            click();
    }

    HoverHandler {
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    Keys.onPressed: event => {
        if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter)
            return;
        event.accepted = true;
        if (!event.isAutoRepeat)
            click();
    }

    background: Rectangle {
        id: surface
        color: "transparent"
        radius: Style.cornerRadiusM
    }
}
