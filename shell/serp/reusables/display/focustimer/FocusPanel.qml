// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
import QtQuick
import "../../../"

Item {
    property bool isActiveTab: false
    property bool widgetVisible: visible
    property var scaleFn: null
    property real contentGap: -1
    property real padding: s(10)

    function s(val) {
        return typeof scaleFn === "function" ? scaleFn(val) : val;
    }

    function alpha(color, a) { return Qt.rgba(color.r, color.g, color.b, a); }
}
