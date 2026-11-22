// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
import QtQuick
import Quickshell
import Quickshell.Wayland

Scope {
    id: dimScope

    property bool active: false

    Variants {
        model: Quickshell.screens

        PanelWindow {
            id: dimWindow
            required property var modelData

            screen: modelData
            visible: dimRect.opacity > 0
            color: "transparent"

            anchors {
                top: true
                bottom: true
                left: true
                right: true
            }

            WlrLayershell.layer: WlrLayer.Overlay
            exclusionMode: ExclusionMode.Ignore

            mask: Region {}

            Rectangle {
                id: dimRect
                anchors.fill: parent
                color: "black"
                opacity: dimScope.active ? 0.5 : 0.0

                Behavior on opacity {
                    NumberAnimation {
                        duration: 250
                        easing.type: Easing.InOutQuad
                    }
                }
            }
        }
    }
}
