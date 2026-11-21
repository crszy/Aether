// Mix & match bar piece: any DankMaterialShell bar widget ("dms:<Widget>" in bar.json entries, e.g.
// "dms:WorkspaceSwitcher", "dms:CpuMonitor", "dms:SystemTrayBar" - the file names in dms/Modules/DBar/Widgets).
// Bar.qml sets screen + widgetId; the widget gets a left-edge axis so it lays itself out vertically.
import QtQuick
import Quickshell
import qs.dms.Modules.DBar

Item {
    id: root

    property ShellScreen screen
    property string widgetId: ""
    property bool vertical: true // false = a horizontal bar (e.g. end-4's)

    // DMS pills size themselves with width/height (not implicit sizes)
    implicitWidth: widget.item?.width ?? 0
    implicitHeight: widget.item?.height ?? 0

    AxisContext {
        id: leftAxis

        edge: root.vertical ? "left" : "top"
    }

    Loader {
        id: widget

        anchors.centerIn: parent
        active: root.widgetId !== "" && !!root.screen
        source: active ? Qt.resolvedUrl(`../../../dms/Modules/DBar/Widgets/${root.widgetId}.qml`) : ""
        onLoaded: {
            const props = root.vertical ? { axis: leftAxis, section: "left", widgetThickness: 30, barThickness: 44, isLeftBarEdge: true } : { axis: leftAxis, section: "right", widgetThickness: 30, barThickness: 40, isTopBarEdge: true };
            for (const k in props)
                if (k in item)
                    item[k] = props[k];
            if ("parentScreen" in item)
                item.parentScreen = Qt.binding(() => root.screen);
            if ("screen" in item)
                item.screen = Qt.binding(() => root.screen);
        }
    }
}
