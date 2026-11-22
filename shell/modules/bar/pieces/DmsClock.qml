// Mix & match bar piece ("dms:clock" in bar.json entries): DankMaterialShell's analog clock face.
// Loaded by URL from Bar.qml only when listed.
import QtQuick
import qs.dms.DCommon.Widgets

Item {
    id: root

    property var now: new Date()

    implicitWidth: 36
    implicitHeight: 36

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: root.now = new Date()
    }

    DClockWidget {
        anchors.fill: parent
        now: root.now
        cfg: ({ style: "analog" })
    }
}
