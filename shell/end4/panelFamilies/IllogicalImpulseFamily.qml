import QtQuick
import Quickshell

import qs.end4.modules.common
import qs.end4.modules.ii.background
import qs.end4.modules.ii.bar
import qs.end4.modules.ii.cheatsheet
import qs.end4.modules.ii.dock
import qs.end4.modules.ii.lock
import qs.end4.modules.ii.mediaControls
import qs.end4.modules.ii.notificationPopup
import qs.end4.modules.ii.onScreenDisplay
import qs.end4.modules.ii.onScreenKeyboard
import qs.end4.modules.ii.overview
import qs.end4.modules.ii.polkit
import qs.end4.modules.ii.regionSelector
import qs.end4.modules.ii.screenCorners
import qs.end4.modules.ii.screenTranslator
import qs.end4.modules.ii.sessionScreen
import qs.end4.modules.ii.sidebarLeft
import qs.end4.modules.ii.sidebarRight
import qs.end4.modules.ii.overlay
import qs.end4.modules.ii.verticalBar
import qs.end4.modules.ii.wallpaperSelector

Scope {
    PanelLoader { extraCondition: !Config.options.bar.vertical; component: Bar {} }
    PanelLoader { component: Background {} }
    PanelLoader { component: Cheatsheet {} }
    PanelLoader { extraCondition: Config.options.dock.enable; component: Dock {} }
    PanelLoader { component: Lock {} }
    PanelLoader { component: MediaControls {} }
    PanelLoader { component: NotificationPopup {} }
    PanelLoader { component: OnScreenDisplay {} }
    PanelLoader { component: OnScreenKeyboard {} }
    PanelLoader { component: Overlay {} }
    PanelLoader { component: Overview {} }
    PanelLoader { component: Polkit {} }
    PanelLoader { component: RegionSelector {} }
    PanelLoader { component: ScreenCorners {} }
    PanelLoader { component: ScreenTranslator {} }
    PanelLoader { component: SessionScreen {} }
    PanelLoader { component: SidebarLeft {} }
    PanelLoader { component: SidebarRight {} }
    PanelLoader { extraCondition: Config.options.bar.vertical; component: VerticalBar {} }
    PanelLoader { component: WallpaperSelector {} }
}
