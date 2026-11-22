// Mix & match piece (WinSettings.lockClock = "noctalia"): Noctalia's analog clock on Caelestia's lock.
// Loaded by URL from Center.qml only when picked.
import QtQuick
import qs.noctalia.Widgets

Item {
    implicitWidth: 240
    implicitHeight: 240

    NClock {
        anchors.fill: parent
        clockStyle: "analog"
    }
}
