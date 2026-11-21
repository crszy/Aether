import QtQuick

// Windows stand-in for Quickshell.Wayland's ShortcutInhibitor (keyboard shortcuts are the shell's own here)
QtObject {
    property var window: null
    property bool enabled: false
    readonly property bool active: false
}
