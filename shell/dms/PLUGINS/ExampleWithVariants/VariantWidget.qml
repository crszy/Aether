import QtQuick
import qs.dms.Common
import qs.dms.Services
import qs.dms.DCommon.Widgets
import qs.dms.Modules.Plugins

PluginComponent {
    id: root

    property string variantId: ""
    property var variantData: null

    property string displayText: variantData?.text || "Default Text"
    property string displayIcon: variantData?.icon || "widgets"

    horizontalBarPill: Component {
        Row {
            spacing: Theme.spacingXS

            DIcon {
                name: root.displayIcon
                size: root.iconSize
                color: Theme.surfaceText
                anchors.verticalCenter: parent.verticalCenter
            }

            StyledText {
                text: root.displayText
                font.pixelSize: root.textSize
                font.weight: Theme.fontWeightMedium
                color: Theme.surfaceText
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    verticalBarPill: Component {
        Column {
            spacing: 1

            DIcon {
                name: root.displayIcon
                size: root.iconSize
                color: Theme.surfaceText
                anchors.horizontalCenter: parent.horizontalCenter
            }

            StyledText {
                text: root.displayText
                font.pixelSize: root.textSize
                font.weight: Theme.fontWeightMedium
                color: Theme.surfaceText
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }
}
