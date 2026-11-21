pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell

Singleton {
    property var session: null
    property var cache: null
    property var files: null
    // lookup(path) returns {color, emblem, icon} or null
    property var appearance: null
    property bool hyprlandFocusGrab: false
    property bool ownWindowDecorations: true
}
