import QtQuick
import qs.dms.Common
import qs.dms.Modules.Plugins
import qs.dms.DCommon.Widgets

BasePill {
    id: root

    property bool isActive: false

    content: Component {
        Item {
            implicitWidth: icon.width
            implicitHeight: root.contentThickness

            DIcon {
                id: icon
                anchors.centerIn: parent
                name: "power_settings_new"
                size: Theme.barIconSize(root.barThickness, undefined, root.barConfig?.maximizeWidgetIcons, root.barConfig?.iconScale)
                color: Theme.widgetIconColor
            }
        }
    }
}
