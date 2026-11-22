pragma Singleton

// Opens the rice settings panel (one window, reused); WindowFactory sends here when Settings style = "rice".
import QtQuick
import Quickshell
import Quickshell.Windows as Win

Singleton {
    id: root

    property var current: null

    function open(tab: string): void {
        if (root.current) {
            if (tab === "setup") {
                root.current.step = 0;
                root.current.setup = true;
            } else if (tab) {
                root.current.tab = tab;
            }
            root.current.visible = true;
            root.current.applyFrame();
            return;
        }
        root.current = comp.createObject(root, { startTab: tab || "overview" });
    }

    Component {
        id: comp

        RiceWindow {
            visible: true
            onVisibleChanged: if (!visible) {
                root.current = null;
                destroy();
            }
        }
    }
}
