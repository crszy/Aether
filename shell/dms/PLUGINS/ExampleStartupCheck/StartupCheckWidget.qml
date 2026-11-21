import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets
import qs.dms.Modules.Plugins

PluginComponent {
    id: root

    layerNamespacePlugin: "startup-check"

    horizontalBarPill: Component {
        Row {
            spacing: Theme.spacingXS

            DIcon {
                name: "verified_user"
                size: root.iconSize
                color: Theme.primary
                anchors.verticalCenter: parent.verticalCenter
            }

            StyledText {
                text: "boregard"
                font.pixelSize: root.textSize
                color: Theme.surfaceText
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    verticalBarPill: Component {
        DIcon {
            name: "verified_user"
            size: root.iconSize
            color: Theme.primary
        }
    }
}
