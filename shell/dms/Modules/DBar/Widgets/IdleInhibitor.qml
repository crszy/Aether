import QtQuick
import qs.dms.Common
import qs.dms.Modules.Plugins
import qs.dms.Services
import qs.dms.DCommon.Widgets

BasePill {
    id: root

    property color idleColor: Theme.widgetTextColor
    property color inhibitColor: Theme.primary

    content: Component {
        Item {
            implicitWidth: icon.width
            implicitHeight: root.contentThickness

            DIcon {
                id: icon
                anchors.centerIn: parent
                name: "local_cafe"
                size: Theme.barIconSize(root.barThickness, -4, root.barConfig?.maximizeWidgetIcons, root.barConfig?.iconScale)
                filled: SessionService.idleInhibited
                color: SessionService.idleInhibited ? inhibitColor : idleColor
            }
        }
    }

    onClicked: SessionService.toggleIdleInhibit()
}
