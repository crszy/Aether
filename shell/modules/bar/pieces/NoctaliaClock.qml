// Mix & match bar piece ("noctalia:clock" in bar.json entries): Noctalia's analog clock.
// Loaded by URL from Bar.qml only when listed.
import QtQuick
import qs.noctalia.Widgets

Item {
    implicitWidth: 34
    implicitHeight: 34

    NClock {
        anchors.fill: parent
        clockStyle: "analog"
    }
}
