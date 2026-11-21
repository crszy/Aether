import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Hyprland
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.widgets

Scope {
    id: root

    Connections {
        target: GlobalStates

        function onSidebarLeftOpenChanged() {
            if (GlobalStates.sidebarLeftOpen)
                panelLoader.active = true;
        }
    }

    Loader {
        id: panelLoader
        active: GlobalStates.sidebarLeftOpen
        sourceComponent: PanelWindow {
            id: panelWindow
            exclusiveZone: 0
            WlrLayershell.namespace: "quickshell:actionCenter"
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand
            color: "transparent"

            anchors {
                bottom: Config.options.waffles.bar.bottom
                top: !Config.options.waffles.bar.bottom
                right: true
            }

            implicitWidth: content.implicitWidth
            implicitHeight: content.implicitHeight

            HyprlandFocusGrab {
                id: focusGrab
                active: true
                windows: [panelWindow]
                onCleared: content.close()
            }

            Connections {
                target: GlobalStates
                function onSidebarLeftOpenChanged() {
                    if (!GlobalStates.sidebarLeftOpen)
                        content.close();
                }
            }

            ActionCenterContent {
                id: content
                anchors.fill: parent

                onClosed: {
                    GlobalStates.sidebarLeftOpen = false;
                    panelLoader.active = false;
                }
            }
        }
    }

    function toggleOpen() {
        GlobalStates.sidebarLeftOpen = !GlobalStates.sidebarLeftOpen;
    }

    IpcHandler {
        target: "end4:sidebarLeft"

        function toggle() {
            root.toggleOpen();
        }
    }

    GlobalShortcut {
        name: "sidebarLeftToggle"
        description: "Toggles left sidebar on press"

        onPressed: root.toggleOpen()
    }

    IpcHandler {
        target: "end4:mediaControls"

        function toggle(): void {
            GlobalStates.sidebarLeftOpen = !GlobalStates.sidebarLeftOpen;
        }
    }

    GlobalShortcut {
        name: "mediaControlsToggle"
        description: "Toggles media controls on press"

        onPressed: {
            GlobalStates.sidebarLeftOpen = !GlobalStates.sidebarLeftOpen;
        }
    }
}
