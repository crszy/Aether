import QtQuick
import qs.dms.Common
import qs.dms.Modules.Plugins
import qs.dms.Services
import qs.dms.DCommon.Widgets

BasePill {
    id: root

    opacity: DMSService.capsLockState ? 1 : 0

    states: [
        State {
            name: "hidden_horizontal"
            when: !DMSService.capsLockState && !isVerticalOrientation
            PropertyChanges {
                target: root
                width: 0
            }
        },
        State {
            name: "hidden_vertical"
            when: !DMSService.capsLockState && isVerticalOrientation
            PropertyChanges {
                target: root
                height: 0
            }
        }
    ]

    transitions: [
        Transition {
            NumberAnimation {
                properties: "width,height"
                duration: Theme.shortDuration
                easing.type: Theme.standardEasing
            }
        }
    ]

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.shortDuration
            easing.type: Theme.standardEasing
        }
    }

    content: Component {
        Item {
            implicitWidth: icon.width
            implicitHeight: root.contentThickness

            DIcon {
                id: icon
                anchors.centerIn: parent
                name: "shift_lock"
                size: Theme.barIconSize(root.barThickness, undefined, root.barConfig?.maximizeWidgetIcons, root.barConfig?.iconScale)
                color: Theme.primary
            }
        }
    }
}
