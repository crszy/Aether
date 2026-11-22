// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
import QtQuick
import qs.services as Cae
import Quickshell
import "../"

Item {
    id: root

    Loader { active: Cae.WinSettings.mixOsd === "serp"; sourceComponent: Component { Osd {} } } // Windows: Mix & match
    TrayBase {}
    SideMusicPopout {}
    DesktopMenu {}
    DockAppMenu {}
}
