import QtQuick
import Quickshell

import qs.end4.modules.common
import qs.end4.modules.waffle.actionCenter
import qs.end4.modules.waffle.background
import qs.end4.modules.waffle.bar
import qs.end4.modules.waffle.lock
import qs.end4.modules.waffle.notificationCenter
import qs.end4.modules.waffle.notificationPopup
import qs.end4.modules.waffle.onScreenDisplay
// import qs.modules.waffle.overlay
import qs.end4.modules.waffle.polkit
import qs.end4.modules.waffle.screenSnip
import qs.end4.modules.waffle.startMenu
import qs.end4.modules.waffle.sessionScreen
import qs.end4.modules.waffle.taskView

// Fallbacks
import qs.end4.modules.ii.cheatsheet
import qs.end4.modules.ii.onScreenKeyboard
import qs.end4.modules.ii.overlay
import qs.end4.modules.ii.screenTranslator
import qs.end4.modules.ii.wallpaperSelector

Scope {
    PanelLoader { component: WaffleActionCenter {} }
    PanelLoader { component: WaffleBar {} }
    PanelLoader { component: WaffleBackground {} }
    PanelLoader { component: WaffleLock {} }
    PanelLoader { component: WaffleNotificationCenter {} }
    PanelLoader { component: WaffleNotificationPopup {} }
    PanelLoader { component: WaffleOSD {} }
    // PanelLoader { component: WaffleOverlay {} }
    PanelLoader { component: WafflePolkit {} }
    PanelLoader { component: WScreenSnip {} }
    PanelLoader { component: WaffleStartMenu {} }
    PanelLoader { component: WaffleSessionScreen {} }
    PanelLoader { component: WaffleTaskView {} }

    PanelLoader { component: Cheatsheet {} }
    PanelLoader { component: OnScreenKeyboard {} }
    PanelLoader { component: Overlay {} }
    PanelLoader { component: ScreenTranslator {} }
    PanelLoader { component: WallpaperSelector {} }
}
