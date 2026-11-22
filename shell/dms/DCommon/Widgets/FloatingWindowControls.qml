import QtQuick
import Quickshell.Windows as WinFrame
import qs.dms.DCommon.Common

Item {
    id: root

    readonly property real edgeSize: Style.spacingS
    required property var targetWindow
    readonly property bool supported: typeof targetWindow.startSystemMove === "function"
    readonly property bool canMaximize: targetWindow.minimumSize.width !== targetWindow.maximumSize.width || targetWindow.minimumSize.height !== targetWindow.maximumSize.height
    readonly property bool canMinimize: targetWindow.minimized !== undefined // Windows: minimizing works
    readonly property bool ownDecorations: Host.ownWindowDecorations
    readonly property bool frameless: ownDecorations // Windows: DMS draws its own title bar
    readonly property var backingWindow: Window.window

    anchors.fill: parent

    onFramelessChanged: applyDecorationHint()
    onBackingWindowChanged: applyDecorationHint()
    Component.onCompleted: Qt.callLater(applyDecorationHint) // Windows
    Connections {
        target: root.backingWindow
        ignoreUnknownSignals: true
        function onVisibleChanged() {
            if (root.backingWindow?.visible)
                Qt.callLater(root.applyDecorationHint);
        }
    }

    // Qt cannot re-create the xdg-decoration object on a mapped window, so clearing the hint lands on the next map.
    function applyDecorationHint() {
        if (!backingWindow)
            return;
        if (frameless) WinFrame.WindowFrame.hideTitleBar(backingWindow); else WinFrame.WindowFrame.showTitleBar(backingWindow); // Windows
        return;
        const current = (backingWindow.flags & Qt.FramelessWindowHint) !== 0;
        if (current === frameless)
            return;
        backingWindow.flags = frameless ? backingWindow.flags | Qt.FramelessWindowHint : backingWindow.flags & ~Qt.FramelessWindowHint;
    }

    function tryStartMove() {
        targetWindow.startSystemMove();
    }

    function tryMinimize() {
        if (!canMinimize)
            return;
        targetWindow.minimized = true;
    }

    function tryStartResize(edges) {
        if (!canMaximize)
            return;
        targetWindow.startSystemResize(edges);
    }

    function tryToggleMaximize() {
        if (!canMaximize)
            return;
        targetWindow.maximized = !targetWindow.maximized;
    }

    MouseArea {
        visible: root.canMaximize
        height: root.edgeSize
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.edgeSize - Style.spacingXXS
        anchors.rightMargin: root.edgeSize - Style.spacingXXS
        cursorShape: Qt.SizeVerCursor
        onPressed: root.tryStartResize(Qt.TopEdge)
    }

    MouseArea {
        visible: root.canMaximize
        width: root.edgeSize
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.edgeSize - Style.spacingXXS
        anchors.bottomMargin: root.edgeSize - Style.spacingXXS
        cursorShape: Qt.SizeHorCursor
        onPressed: root.tryStartResize(Qt.LeftEdge)
    }

    MouseArea {
        visible: root.canMaximize
        width: root.edgeSize
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.edgeSize - Style.spacingXXS
        anchors.bottomMargin: root.edgeSize - Style.spacingXXS
        cursorShape: Qt.SizeHorCursor
        onPressed: root.tryStartResize(Qt.RightEdge)
    }

    MouseArea {
        visible: root.canMaximize
        width: root.edgeSize
        height: root.edgeSize
        anchors.left: parent.left
        anchors.top: parent.top
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.tryStartResize(Qt.LeftEdge | Qt.TopEdge)
    }

    MouseArea {
        visible: root.canMaximize
        width: root.edgeSize
        height: root.edgeSize
        anchors.right: parent.right
        anchors.top: parent.top
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.tryStartResize(Qt.RightEdge | Qt.TopEdge)
    }

    MouseArea {
        visible: root.canMaximize
        height: root.edgeSize
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.edgeSize - Style.spacingXXS
        anchors.rightMargin: root.edgeSize - Style.spacingXXS
        cursorShape: Qt.SizeVerCursor
        onPressed: root.tryStartResize(Qt.BottomEdge)
    }

    MouseArea {
        visible: root.canMaximize
        width: root.edgeSize
        height: root.edgeSize
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.tryStartResize(Qt.LeftEdge | Qt.BottomEdge)
    }

    MouseArea {
        visible: root.canMaximize
        width: root.edgeSize
        height: root.edgeSize
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.tryStartResize(Qt.RightEdge | Qt.BottomEdge)
    }
}
