// services/QuickSettingsService.qml
import QtQuick
import Quickshell
import "./"

pragma Singleton

Item {
    id: service

    property bool qsVisible: false
    property string activeTab: "network"
    property rect lastRect: Qt.rect(0, 0, 0, 0)

    onQsVisibleChanged: Variables.quickSettingsOpen = qsVisible

    function open(tab, rect) {
        CenterState.close();
        DynamicIslandService.close();

        if (tab && tab !== "") {
            activeTab = tab;
        }

        if (rect && rect.width > 0) {
            lastRect = rect;
        }

        qsVisible = true;
    }

    function toggle(tab, rect) {
        let targetTab = (tab && tab !== "") ? tab : "network";
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
