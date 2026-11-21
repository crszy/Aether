// Mix & match bar piece: an end-4 bar widget that works in a vertical bar ("end4:<Widget>" in bar.json
// entries), drawn in end-4's own pill group. Only widgets end-4 itself uses in its vertical bar are listed;
// its other bar widgets are horizontal-only.
import QtQuick
import QtQuick.Layouts
import Quickshell
import qs.end4.modules.ii.bar as E4Bar

E4Bar.BarGroup {
    id: root

    property ShellScreen screen
    property string widgetId: ""

    // name in bar.json -> file under end4/modules/ii, and whether it has a "vertical" switch
    readonly property var widgets: ({
        Workspaces: ["bar/Workspaces", true],
        SysTray: ["bar/SysTray", true],
        HyprlandXkbIndicator: ["bar/HyprlandXkbIndicator", true],
        LeftSidebarButton: ["bar/LeftSidebarButton", false],
        Resources: ["verticalBar/Resources", false],
        Media: ["verticalBar/VerticalMedia", false],
        Clock: ["verticalBar/VerticalClockWidget", false],
        BatteryIndicator: ["verticalBar/BatteryIndicator", false]
    })
    readonly property var entry: widgets[widgetId] ?? null

    vertical: true
    padding: 6
    visible: !!entry

    // "vertical" is handed over at creation, so the widget never lays itself out horizontally first
    onEntryChanged: {
        if (entry)
            widget.setSource(Qt.resolvedUrl(`../../../end4/modules/ii/${entry[0]}.qml`), entry[1] ? { vertical: true } : {});
        else
            widget.source = "";
    }

    Loader {
        id: widget

        Layout.alignment: Qt.AlignHCenter
    }
}
