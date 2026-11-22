// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
import QtQuick
import Quickshell

Variants {
    model: Quickshell.screens

    delegate: Component {
        Scope {
            id: dockScope
            required property var modelData

            DockExclusionWindow {
                modelData: dockScope.modelData
                dockWindow: dockPanel
            }

            DockWindow {
                id: dockPanel
                modelData: dockScope.modelData
            }
        }
    }
}
