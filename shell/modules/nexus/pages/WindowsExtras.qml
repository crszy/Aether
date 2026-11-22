pragma ComponentBehavior: Bound

// Settings -> Windows: title bars, pop-ups, bar colour, Settings style and unlocking.
// (These used to sit on the Mix & match page, which was removed: Setup picks the look now.)
import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import Caelestia.I18n
import qs.components
import qs.components.controls
import qs.services
import qs.modules.nexus.common
import qs.shims
import Quickshell.Windows as Win

ColumnLayout {
    id: root

    Layout.fillWidth: true
    spacing: Tokens.spacing.extraSmall / 2

    readonly property list<MenuItem> titleBarItems: [
        MenuItem { text: Tr.tr("As Windows draws them"); value: "system" },
        MenuItem { text: Tr.tr("Themed (your colours)"); value: "themed" },
        MenuItem { text: Tr.tr("DankMaterialShell's header"); value: "dms" },
        MenuItem { text: Tr.tr("Discord-style"); value: "discord" },
        MenuItem { text: Tr.tr("KDE Plasma (Breeze)"); value: "kde" },
        MenuItem { text: Tr.tr("Ryoku - no title bars (like Hyprland)"); value: "strip" }
    ]
    readonly property list<MenuItem> titleBlendItems: [
        MenuItem { text: Tr.tr("Blend with the app"); value: true },
        MenuItem { text: Tr.tr("The style's own colours"); value: false }
    ]
    readonly property list<MenuItem> titleSizeItems: [
        MenuItem { text: Tr.tr("The style's own"); value: "style" },
        MenuItem { text: Tr.tr("Compact (like Windows)"); value: "compact" },
        MenuItem { text: Tr.tr("Tall"); value: "tall" }
    ]
    readonly property list<MenuItem> titleIconItems: [
        MenuItem { text: Tr.tr("As the style does it"); value: "auto" },
        MenuItem { text: Tr.tr("Show"); value: "on" },
        MenuItem { text: Tr.tr("Hide"); value: "off" }
    ]
    readonly property list<MenuItem> barColourItems: [
        MenuItem { text: "Colour scheme"; value: "" },
        MenuItem { text: "Black"; value: "#0b0b0e" },
        MenuItem { text: "Dark grey"; value: "#1e1e24" },
        MenuItem { text: "Slate"; value: "#2b3040" },
        MenuItem { text: "Navy"; value: "#16203a" },
        MenuItem { text: "Plum"; value: "#2d1b33" },
        MenuItem { text: "Forest"; value: "#16291f" },
        MenuItem { text: "Wine"; value: "#331820" },
        MenuItem { text: "White"; value: "#f2f2f5" }
    ]
    readonly property list<MenuItem> styleItems: [
        MenuItem { text: "Aether (all shells)"; value: "aether" },
        MenuItem { text: "Aether classic"; value: "rice" },
        MenuItem { text: "Caelestia"; value: "caelestia" },
        MenuItem { text: "Noctalia"; value: "noctalia" },
        MenuItem { text: "DMS"; value: "dms" }
    ]

    // [label, description, WinSettings key, menu]
    readonly property var titleRows: [
        [Tr.tr("App title bars"), Tr.tr("Recolour, restyle or remove the title bar Windows draws on apps"), "titleBars", root.titleBarItems],
        [Tr.tr("App icon in title bars"), Tr.tr("The app's icon before its title (DMS, Discord and KDE styles)"), "titleBarIcon", root.titleIconItems],
        [Tr.tr("Title bar size"), Tr.tr("How tall the DMS, Discord and KDE title bars are"), "titleBarSize", root.titleSizeItems],
        [Tr.tr("Title bar colours"), Tr.tr("Take the app's own colour, like Linux apps whose header is part of the window"), "titleBarBlend", root.titleBlendItems]
    ]

    SectionHeader {
        text: Tr.tr("App title bars")
    }

    Repeater {
        model: root.titleRows

        SelectRow {
            required property var modelData
            required property int index

            first: index === 0
            last: index === root.titleRows.length - 1
            label: modelData[0]
            subtext: modelData[1]
            menuItems: modelData[3]
            active: modelData[3].find(i => i.value === WinSettings[modelData[2]]) ?? modelData[3][0]
            onSelected: item => WinSettings[modelData[2]] = item.value
        }
    }

    SectionHeader {
        text: Tr.tr("Pop-ups and bar")
    }

    ToggleRow {
        first: true
        text: Tr.tr("Popups on all monitors")
        subtext: Tr.tr("Pop-ups (volume, mic, brightness, notifications, game mode and other alerts) show on every monitor. Off: only on the main monitor")
        checked: WinSettings.osdAllMonitors
        onToggled: WinSettings.osdAllMonitors = checked
    }

    ToggleRow {
        text: Tr.tr("Game mode by itself")
        subtext: Tr.tr("While a game or another fullscreen app has focus: no window animations, blur or shadows, and video wallpapers pause to free GPU memory. Back to normal when you leave it")
        checked: WinSettings.autoGameMode
        onToggled: WinSettings.autoGameMode = checked
    }

    ToggleRow {
        text: Tr.tr("Game mode: window effects off")
        subtext: Tr.tr("No window animations, blur or shadows while game mode is on")
        checked: WinSettings.gmWmEffects
        onToggled: WinSettings.gmWmEffects = checked
    }

    ToggleRow {
        text: Tr.tr("Game mode: window borders off")
        subtext: Tr.tr("Hides the focus borders around windows while game mode is on")
        checked: WinSettings.gmBorders
        onToggled: WinSettings.gmBorders = checked
    }

    ToggleRow {
        text: Tr.tr("Game mode: audio visualizer off")
        subtext: Tr.tr("Stops the bar's music bars and the sound capture behind them. They redraw the bar 25 times a second")
        checked: WinSettings.gmVisualizer
        onToggled: WinSettings.gmVisualizer = checked
    }

    ToggleRow {
        text: Tr.tr("Game mode: pause moving wallpapers")
        subtext: Tr.tr("Video and animated wallpapers stop and free their GPU memory")
        checked: WinSettings.gmWallpaper
        onToggled: WinSettings.gmWallpaper = checked
    }

    SelectRow {
        last: true
        label: Tr.tr("Caelestia bar colour")
        subtext: Tr.tr("The bar and screen frame. DMS and Noctalia pick theirs in their own bar settings")
        menuItems: root.barColourItems
        active: root.barColourItems.find(i => i.value === WinSettings.barColour) ?? root.barColourItems[0]
        onSelected: item => WinSettings.barColour = item.value
    }

    SectionHeader {
        text: Tr.tr("Settings style")
    }

    SelectRow {
        first: true
        label: Tr.tr("Settings style")
        subtext: Tr.tr("Which settings window opens. This page stays in Caelestia's.")
        menuItems: root.styleItems
        active: root.styleItems.find(i => i.value === WinSettings.settingsStyle) ?? root.styleItems[0]
        onSelected: item => WinSettings.settingsStyle = item.value
    }

    RowButton {
        last: true
        icon: "open_in_new"
        text: Tr.tr("Open Noctalia's settings")
        subtext: WinSettings.noctaliaAny ? Tr.tr("Noctalia's own options (bar widgets, dock, colours ...)") : Tr.tr("Pick a Noctalia look in Setup first")
        disabled: !WinSettings.noctaliaAny
        onClicked: WinSettings.ipc("settings", "toggle", [])
    }

    // ── unlocking: Aether's own fingerprint and security key, for every lock that supports them (Ryoku's qylock,
    //    DMS, Zenith...) - the password always works too
    SectionHeader {
        text: Tr.tr("Unlocking")
    }

    InfoRow {
        first: true
        icon: "fingerprint"
        label: Tr.tr("Fingerprint")
        subtext: Win.Fingerprint.available ? Tr.tr("Sensor found - the lock screens accept the fingerprints you set up in Windows Hello")
            : Tr.tr("No fingerprint sensor found. Set one up in Windows Hello, then reopen this page")
    }

    InfoRow {
        icon: "key"
        label: Tr.tr("Aether key")
        subtext: AetherKey.registered ? Tr.tr("%1 is your key: plugging it in unlocks the lock screen").arg(AetherKey.label)
            : Tr.tr("Pick a USB stick or SD card below to make it a key (a small .aether-key file is written to it)")
    }

    Repeater {
        id: driveRows

        property int refresh: 0
        model: { refresh; return AetherKey.drives(); }

        RowButton {
            required property string modelData

            icon: "usb"
            text: Tr.tr("Use %1 as the Aether key").arg(modelData.slice(0, 2))
            subtext: Tr.tr("Replaces the key you had before")
            onClicked: AetherKey.register(modelData)
        }
    }

    RowButton {
        icon: "refresh"
        text: Tr.tr("Look for drives again")
        subtext: Tr.tr("After plugging a stick in")
        onClicked: driveRows.refresh++
    }

    RowButton {
        last: true
        icon: "key_off"
        text: Tr.tr("Remove the Aether key")
        subtext: AetherKey.registered ? Tr.tr("The stick stops unlocking anything") : Tr.tr("No key registered")
        disabled: !AetherKey.registered
        onClicked: AetherKey.unregister()
    }
}
