import QtQuick
import qs.dms.DCommon.Common

DActionButton {
    id: root

    property bool busy: false

    iconName: busy ? "" : "refresh"
    Accessible.name: tooltipText || I18n.tr("Refresh")
    enabled: !busy

    DSpinner {
        anchors.centerIn: parent
        size: root.iconSize
        strokeWidth: Style.spinnerStrokeWidth
        color: root.iconColor
        running: root.busy && root.visible
        visible: root.busy
    }
}
