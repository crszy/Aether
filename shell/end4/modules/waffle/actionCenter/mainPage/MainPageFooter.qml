import QtQuick
import qs.end4.services as E4Win_
import QtQuick.Layouts
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.waffle.looks
import qs.end4.modules.waffle.actionCenter

FooterRectangle {

    // Battery button
    WBorderlessButton {
        visible: Battery.available
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 12

        contentItem: Row {
            spacing: 4

            FluentIcon {
                anchors.verticalCenter: parent.verticalCenter
                icon: WIcons.batteryLevelIcon
                FluentIcon {
                    anchors.fill: parent
                    icon: WIcons.batteryIcon
                }
            }
            WText {
                anchors.verticalCenter: parent.verticalCenter
                text: `${Math.round(Battery.percentage * 100) || 0}%`
            }
        }
    }

    // Settings button
    WBorderlessButton {
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 12

        onClicked: {
            GlobalStates.sidebarLeftOpen = false;
            E4Win_.WinActions.openSettings();
        }

        contentItem: FluentIcon {
            icon: "settings"
        }
    }
}
