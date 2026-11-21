import QtQuick

// Windows stand-in for Quickshell.I3's I3IpcListener: never sends events
Item {
    property var subscriptions: []
    signal ipcEvent(var event)
}
