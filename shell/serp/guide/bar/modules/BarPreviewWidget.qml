// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
import QtQuick

QtObject {
    property var tab: null
    property string variantId: ""

    readonly property var rootObj: tab ? tab.rootObj : null

    function s(v) { return rootObj ? rootObj.s(v) : v; }
    function cs(v) { return rootObj ? rootObj.s(v) : v; }

    property bool isCompact: false
    property string orientation: tab && tab.isSideBar ? "vertical" : "horizontal"
    property bool isPreview: true
    property bool moduleActive: tab ? tab.visible : false
    property var barWindow: ({
        "startupCascadeFinished": true,
        "isStartupReady": true,
        "isDataReady": true,
        "barOpacity": tab ? tab.barOpacity : 1,
        "s": function(v) { return rootObj ? rootObj.s(v) : v; },
        "cs": function(v) { return rootObj ? rootObj.s(v) : v; }
    })
}
