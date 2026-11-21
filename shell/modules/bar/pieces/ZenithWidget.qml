// Mix & match bar piece: a Zenith bar widget ("zenith:<Widget>"). Zenith's widgets are made for a horizontal
// top bar only, so in a vertical bar this hides itself.
import QtQuick
import Quickshell

Item {
    id: root

    property ShellScreen screen
    property string widgetId: ""
    property bool vertical: true

    readonly property var widgets: ({
        Workspaces: "Left/Workspaces", Tray: "Right/Tray", Resources: "Right/Resources",
        Network: "Right/Network", QuickSettings: "Right/QuickSettingsCluster"
    })
    readonly property string file: widgets[widgetId] ?? ""

    visible: !vertical && file !== ""
    implicitWidth: widget.item?.implicitWidth ?? 0
    implicitHeight: widget.item?.implicitHeight ?? 0

    Loader {
        id: widget

        anchors.centerIn: parent
        active: root.visible
        source: active ? Qt.resolvedUrl(`../../../zenith/bar/${root.file}.qml`) : ""
    }
}
