pragma ComponentBehavior: Bound
import QtQuick
import qs.end4.services as E4Win_
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.waffle.looks
import qs.end4.modules.waffle.startMenu

RowLayout {
    id: root
    property StartMenuContext context

    WPanelIconButton {
        implicitWidth: 36
        implicitHeight: 36
        iconSize: 24
        iconName: "arrow-left"
        onClicked: LauncherSearch.query = ""
    }
    ListView {
        id: tagListView
        Layout.fillWidth: true
        Layout.fillHeight: true
        orientation: Qt.Horizontal
        spacing: 4
        model: root.context.categories
        clip: true
        delegate: WBorderedButton {
            id: tagButton
            required property var modelData
            border.width: 1
            radius: height / 2
            implicitWidth: tagButtonText.implicitWidth + 12 * 2
            implicitHeight: 32
            checked: {
                if (modelData.prefix != "") {
                    return LauncherSearch.query.startsWith(modelData.prefix);
                } else {
                    return !tagListView.model.some(i => (i.prefix != "" && LauncherSearch.query.startsWith(i.prefix)));
                }
            }
            contentItem: Item {
                WText {
                    id: tagButtonText
                    anchors.centerIn: parent
                    color: tagButton.fgColor
                    text: tagButton.modelData.name
                    font.pixelSize: Looks.font.pixelSize.large
                }
            }
            onClicked: LauncherSearch.ensurePrefix(tagButton.modelData.prefix)
        }
    }
    WPanelIconButton {
        id: optionsButton
        implicitWidth: 36
        implicitHeight: 36
        iconSize: 24
        iconName: "more-horizontal"

        onClicked: accountsMenu.open()

        WMenu {
            id: accountsMenu
            x: -accountsMenu.implicitWidth + optionsButton.implicitWidth + 10
            y: optionsButton.height
            downDirection: true
            Action {
                icon.name: "people-settings"
                text: Translation.tr("Manage accounts")
                onTriggered: {
                    E4Win_.WinActions.open(Config.options.apps.manageUser)
                    GlobalStates.searchOpen = false;
                }
            }
        }
    }
}
