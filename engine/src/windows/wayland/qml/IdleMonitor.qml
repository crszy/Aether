import QtQuick
import Quickshell.Windows

// Windows version of Quickshell.Wayland's IdleMonitor: isIdle turns true after `timeout` seconds without keyboard or
// mouse input anywhere (GetLastInputInfo through IdlePower). respectInhibitors is accepted but not used yet.
QtObject {
    id: root

    property bool enabled: true
    property real timeout: 0
    property bool respectInhibitors: true
    readonly property bool isIdle: _idle

    property bool _idle: false
    property Timer _poll: Timer {
        interval: 1000
        repeat: true
        running: root.enabled && root.timeout > 0
        onTriggered: {
            const s = IdlePower.idleSeconds();
            root._idle = s >= 0 && s >= root.timeout;
        }
        onRunningChanged: if (!running) root._idle = false
    }
}
