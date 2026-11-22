// Mix & match piece (WinSettings.lockClock = "dms"): DankMaterialShell's own clock widget on Caelestia's lock.
// Loaded by URL from Center.qml only when picked, so DMS is not compiled for it otherwise.
import QtQuick
import qs.dms.DCommon.Widgets

Item {
    id: root

    property var now: new Date()

    implicitWidth: clock.width
    implicitHeight: clock.height

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: root.now = new Date()
    }

    DClockWidget {
        id: clock

        anchors.centerIn: parent
        width: defaultWidth
        height: defaultHeight
        lockScreen: true
        now: root.now
        cfg: ({ style: "expressive" })
    }
}
