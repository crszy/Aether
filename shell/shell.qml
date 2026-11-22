//@ pragma Env QS_CRASHREPORT_URL=https://github.com/caelestia-dots/shell/issues/new?template=crash.yml
//@ pragma DefaultEnv QS_NO_RELOAD_POPUP=1
//@ pragma DefaultEnv QS_DROP_EXPENSIVE_FONTS=1
//@ pragma DefaultEnv QSG_RENDER_LOOP=threaded
//@ pragma DefaultEnv QT_QUICK_FLICKABLE_WHEEL_DECELERATION=10000

import QtQuick
import Quickshell
import Caelestia.Config
import qs.services
import qs.modules.areapicker
import qs.modules.aetherpicker
import qs.modules.background
import qs.modules.drawers
import qs.modules.lock
import qs.modules.switcher
import qs.modules.waybar
import qs.modules.splash
import qs.modules.welcome
import qs.modules.nexus
import qs.modules.captions
import qs.modules.userpanel   // your own panels (~/.config/caelestia/user/panels)
import qs.modules.aether      // Aether style parts (AetherCard, AetherWavy, AetherBlobs, AetherLogo) - your files can use them
import qs.shims      // Windows stand-ins the ported shells use (IdleMonitor ...)
import qs.dms        // the same for DMS
import qs.end4       // and end-4
import qs.noctalia   // only so the QML scanner finds Noctalia's folders; nothing of it is compiled until Mix & match uses it
import qs.daemons

ShellRoot {
    id: root

    settings.watchFiles: true

    Component.onCompleted: {
        // Force certain singletons to load on shell init instead of lazily
        Notifs;
        WinSettings;
        LinuxCompat; // Windows: the ported shells' wl-copy, xdg-open, wpctl... answered here
        RyokuHost; // Windows: runs Ryoku (its own Quickshell instance) while Mix & match picks it
        ConfigParts;
        UserExt; // your own widgets, panels and launcher commands (~/.config/caelestia/user)
        HyprConf;
        Players;
        Brightness;
        Weather.reload();

        if (GlobalConfig.utilities.vpn.enabled)
            VPN;
    }

    FontLoader {
        source: Quickshell.shellPath("assets/google-sans-flex/GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf")
    }

    Binding {
        target: ShellState
        property: "shellRoot"
        value: root
    }

    // Modules
    LazyLoader {   // Mix & match: Caelestia's wallpaper/desktop unless Noctalia's is picked
        active: WinSettings.mixWallpaper === "caelestia"
        Background {}
    }
    Drawers {}
    AreaPicker {}
    AetherPicker {}
    Lock {
        id: lock
    }
    Switcher {}
    ConfigIntro {}
    LazyLoader {   // end-4 illogical-impulse (built in, GPL-3.0) - only while Mix & match uses one of its parts
        active: WinSettings.end4Any
        source: Quickshell.shellPath("end4/End4Host.qml")
    }
    LazyLoader {   // DankMaterialShell / Dux Shell (built in, MIT) - only while Mix & match uses one of its parts
        active: WinSettings.dmsAny
        source: Quickshell.shellPath("dms/DmsHost.qml")
    }
    LazyLoader {   // Zenith (built in, public domain) - only while Mix & match uses one of its parts
        active: WinSettings.zenithAny
        source: Quickshell.shellPath("zenith/ZenithShell.qml")
    }
    LazyLoader {   // Noctalia v4 (built in, MIT) - only loaded (even compiled) when Settings > Mix & match uses one of its parts
        active: WinSettings.noctaliaAny
        source: Quickshell.shellPath("noctalia/NoctaliaShell.qml")
    }
    Splash {}
    UserPanels {}
    UserOsd {}       // your own volume / brightness pop-up (mixOsd "user:<Name>")
    UserDesktop {}   // your own desktop widgets (user/desktop)
    RyokuPieceHost {} // your bar pieces on Ryoku's bar (while Ryoku draws the bar)
    UserMods {}      // your mods: code that runs with Aether (user/mods, like Windhawk mods)
    UserTerminals {} // your terminals in your colours (user/terminals.json: Alacritty, Windows Terminal, templates)
    LazyLoader {   // title bars drawn by the shell over apps (Mix & match > Title bars: DankMaterialShell's, Discord-style)
        active: WinSettings.captionSkin
        Captions {}
    }
    LazyLoader {   // Waybar for Windows (Settings > Setup / Windows), config in ~\.config\waybar
        active: WinSettings.waybar
        Waybar {}
    }

    // first start after Aether's installer: open the "Make it yours" setup once the settings are read
    Timer {
        running: WinSettings.loaded && WinSettings.setupPending
        interval: 2500
        onTriggered: {
            WinSettings.setupPending = false;
            WindowFactory.create(null, { caelestia: true, startPage: "Setup" });
        }
    }

    // Daemons
    Shortcuts {}
    BatteryMonitor {}
    GameMode {}
    IdleInhibit {}
    // IdleMonitors: not on Windows (no IdleMonitor type; Windows handles idle lock/sleep)
}
