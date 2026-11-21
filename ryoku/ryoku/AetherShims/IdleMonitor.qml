import QtQuick
import Quickshell.Windows as Win

// Windows stand-in for Quickshell.Wayland's IdleMonitor (ext-idle-notify), used by the ported shells
// (Zenith, DMS, Noctalia): isIdle turns true after `timeout` seconds without keyboard/mouse input and back to
// false on the next input. Polls Windows' last-input time (GetLastInputInfo) once a second while enabled.
QtObject {
    id: root

    property bool enabled: true
    property real timeout: 300            // seconds
    property bool respectInhibitors: true // Windows' own inhibitors (video, calls) are not visible here
    property bool isIdle: false

    property Timer _poll: Timer {
        interval: 1000
        repeat: true
        running: root.enabled && root.timeout > 0
        onRunningChanged: if (!running) root.isIdle = false
        onTriggered: {
            const idle = Win.IdlePower.idleSeconds() >= root.timeout;
            if (idle !== root.isIdle)
                root.isIdle = idle;
        }
    }
}
