// services/CenterState.qml
import QtQuick
import Quickshell
import "../Settings"

pragma Singleton

Item {
    id: root

    property bool qsVisible: false
    property string activeTab: "Default"
    // Sub-tool for the Focus tab: "Todo", "Roadmap", "Timer", or "Services".
    property string focusTool: "Todo"
    // Sub-tab for the Wallpaper page: "Wallpaper", "Themes", "Slideshow", "Animated"
    property string wallpaperSubTab: "Wallpaper"
    property rect anchorRect: Qt.rect(0, 0, 0, 0)

    function open(tab, rect) {
        let targetTab = (tab && tab !== "") ? tab : "Default";
        if (targetTab === "Theme" || targetTab === "Themes") {
            targetTab = "Wallpaper";
            wallpaperSubTab = "Themes";
        }
        activeTab = targetTab;
        if (rect !== undefined) anchorRect = rect;
        
        DynamicIslandService.close();
        QuickSettingsService.close();
        qsVisible = true;
    }

    function toggle(tab, rect) {
        let targetTab = (tab && tab !== "") ? tab : "Default";
        if (targetTab === "Theme" || targetTab === "Themes") {
            if (qsVisible && activeTab === "Wallpaper" && wallpaperSubTab === "Themes") {
                close();
                return;
            }
            open("Theme", rect);
            return;
        }
        if (qsVisible && activeTab === targetTab) {
            close();
        } else {
            open(targetTab, rect);
        }
    }

    // The menu binds MenuWindow.shown to this flag and owns its own
    // visibility (so the exit animation can finish). Setting the flag is the
    // whole job.
    function close() {
        qsVisible = false;
    }
}
