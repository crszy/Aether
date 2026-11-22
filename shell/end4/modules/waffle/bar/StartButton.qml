import QtQuick
import qs.end4.services as E4Win_
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.waffle.looks

// TODO: Replace the icon with QMLized svg (with /usr/lib/qt6/bin/svgtoqml) for proper micro-animation
AppButton {
    id: root

    leftInset: Config.options.waffles.bar.leftAlignApps ? 12 : 0
    iconName: down ? "start-here-pressed" : "start-here"

    checked: GlobalStates.searchOpen && LauncherSearch.query === ""
    onClicked: {
        GlobalStates.searchOpen = !GlobalStates.searchOpen;
    }

    BarToolTip {
        id: tooltip
        text: Translation.tr("Start")
        extraVisibleCondition: root.shouldShowTooltip
    }

    altAction: () => {
        contextMenu.active = true;
    }

    BarMenu {
        id: contextMenu

        model: [
            {
                text: Translation.tr("Terminal"),
                action: () => {
                    E4Win_.WinActions.open(Config.options.apps.terminal);
                }
            },
            {
                text: Translation.tr("Task Manager"),
                action: () => {
                    E4Win_.WinActions.open(Config.options.apps.taskManager);
                }
            },
            {
                text: Translation.tr("Settings"),
                action: () => {
                    E4Win_.WinActions.openSettings();
                }
            },
            {
                text: Translation.tr("File Explorer"),
                action: () => {
                    Qt.openUrlExternally(Directories.home);
                }
            },
            {
                text: Translation.tr("Search"),
                action: () => {
                    E4Win_.WinActions.end4Ipc("overview", "toggle");
                }
            },
        ]
    }
}
