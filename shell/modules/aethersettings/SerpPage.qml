// serpantinum's own settings (its guide: General, Bar, Display, Dock, Launcher, OSD, Notifications, Idle, Theme,
// Wallpaper, Wellbeing ...) as a page of the merged Aether Settings panel. The guide is serp's code
// (serp/guide, AGPL-3.0, Copyright (C) 2026 Illia Miroshnichenko); it writes ~/.config/serpantinum/settings.json,
// which the serp parts (Mix & match = "serp") read live.
import QtQuick
import Quickshell

Item {
    id: page

    property string startTab: ""

    Loader {
        id: guide
        anchors.fill: parent
        anchors.margins: 8
        asynchronous: true
        source: Quickshell.shellPath("serp/guide/GuidePopup.qml")
        onLoaded: {
            item.visible = true;
            if (page.startTab && item.gotoTab !== undefined)
                item.gotoTab(page.startTab);
        }
    }

    T {
        visible: guide.status === Loader.Error
        anchors.centerIn: parent
        text: "serpantinum's settings could not load - see the qs log"
        color: M3.inkDim
    }
}
