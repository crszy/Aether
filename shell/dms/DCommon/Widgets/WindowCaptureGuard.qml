import QtQuick
import QtQuick.Window

Item {
    id: root

    required property var targetWindow
    readonly property var renderWindow: targetWindow?.contentItem.Window.window ?? null
    property bool active: false
    property bool pending: false
    property bool waitingForSwap: false

    signal ready

    function prepare() {
        if (pending)
            return;
        active = true;
        if (!renderWindow || !targetWindow.backingWindowVisible) {
            ready();
            return;
        }
        pending = true;
        waitingForSwap = false;
        renderWindow?.update();
    }

    function cancel() {
        pending = false;
        waitingForSwap = false;
        active = false;
    }

    Connections {
        target: root.pending ? root.renderWindow : null

        function onAfterAnimating() {
            if (!root.pending)
                return;
            root.waitingForSwap = true;
        }

        function onFrameSwapped() {
            if (!root.pending || !root.waitingForSwap)
                return;
            root.pending = false;
            root.waitingForSwap = false;
            root.ready();
        }
    }
}
