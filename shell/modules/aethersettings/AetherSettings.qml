pragma Singleton

// Opens Aether settings (one window, reused); WindowFactory and `qs ipc call aether open <tab>` send here when
// Settings style = "aether".
import QtQuick
import Quickshell

Singleton {
    id: root

    property var current: null

    function open(tab: string): void {
        if (root.current && !root.current.closing) {
            root.current.open(tab);
            return;
        }
        root.current = comp.createObject(root, { startTab: tab || "" });
    }

    Component {
        id: comp

        AetherSettingsWindow {
            id: w
            visible: true
            // hide first, destroy a moment later (destroying in the same instant hung the GUI thread in the Nexus)
            onVisibleChanged: if (!visible) {
                closing = true;
                if (root.current === w)
                    root.current = null;
                killer.start();
            }
            Timer {
                id: killer
                interval: 400
                onTriggered: w.destroy()
            }
        }
    }
}
