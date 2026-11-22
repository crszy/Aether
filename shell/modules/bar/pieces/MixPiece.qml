// Mix & match: one bar piece from any shell by id ("noctalia:Tray", "dms:CpuMonitor", "zenith:Resources", ...),
// "user:<Name>" = your own widgets/<Name>.qml in ~/.config/caelestia/user;
// for bars other than Caelestia's (e.g. end-4's, see end4-port.js). Unknown ids show nothing.
import QtQuick
import Quickshell

Item {
    id: root

    property ShellScreen screen
    property string pieceId: ""
    property bool vertical: false

    readonly property var wrappers: ({ noctalia: "NoctaliaWidget", dms: "DmsWidget", zenith: "ZenithWidget", end4: "End4Widget", user: "UserWidget" })

    implicitWidth: piece.item?.implicitWidth ?? 0
    implicitHeight: piece.item?.implicitHeight ?? 0

    onPieceIdChanged: load()
    onScreenChanged: load()

    function load(): void {
        // (worked out here, not in bindings: those are not updated yet when pieceIdChanged runs)
        const sep = pieceId.indexOf(":");
        const w = sep > 0 ? wrappers[pieceId.slice(0, sep)] : undefined;
        const widgetId = pieceId.slice(sep + 1);
        if (!w || !screen || !(w === "UserWidget" ? /^[A-Za-z0-9][\w-]*$/ : /^[A-Z]\w*$/).test(widgetId)) {
            piece.source = "";
            return;
        }
        // vertical goes in at creation: the wrappers pick their layout when they load the widget
        piece.setSource(Qt.resolvedUrl(`${w}.qml`), { vertical: root.vertical, screen: root.screen, widgetId: widgetId });
    }

    Loader {
        id: piece

        anchors.centerIn: parent
    }
}
