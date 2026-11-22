// Mix & match bar piece: any Noctalia bar widget ("noctalia:<Widget>" in bar.json entries, e.g.
// "noctalia:Workspace", "noctalia:Tray", "noctalia:SystemMonitor" - the file names in noctalia/Modules/Bar/Widgets).
// Bar.qml sets screen + widgetId; the widget is told it sits in a vertical (left) bar.
import QtQuick
import Quickshell

Item {
    id: root

    property ShellScreen screen
    property string widgetId: ""
    property bool vertical: true // false = a horizontal bar (e.g. end-4's)

    implicitWidth: widget.item?.implicitWidth ?? 0
    implicitHeight: widget.item?.implicitHeight ?? 0

    Loader {
        id: widget

        anchors.centerIn: parent
        active: root.widgetId !== "" && !!root.screen
        source: active ? Qt.resolvedUrl(`../../../noctalia/Modules/Bar/Widgets/${root.widgetId}.qml`) : ""
        onLoaded: {
            item.screen = Qt.binding(() => root.screen);
            item.widgetId = root.widgetId;
            if ("aetherVertical" in item)
                item.aetherVertical = root.vertical;
        }
    }
}
