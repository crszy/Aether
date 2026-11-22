pragma Singleton

import QtQuick
import Caelestia.I18n

QtObject {
    id: root

    readonly property list<var> pages: [
        // Appearance
        {
            label: Tr.tr("Wallpaper & style"),
            icon: "palette",
            description: Tr.tr("Wallpaper, fonts, colours"),
            category: "appearance"
        },
        {
            label: Tr.tr("Look & feel"),
            icon: "brush",
            description: Tr.tr("Size, bar width, animations, wallpaper effects, loading screen"),
            category: "appearance"
        },

        // Connectivity
        {
            label: Tr.tr("Displays"),
            icon: "monitor",
            description: Tr.tr("Turn monitors on or off, scale, main display"),
            category: "connectivity"
        },
        {
            label: Tr.tr("Network"),
            icon: "wifi",
            description: Tr.tr("Wi-Fi, ethernet, VPN"),
            category: "connectivity"
        },
        {
            label: Tr.tr("Connected devices"),
            icon: "devices_other",
            description: Tr.tr("Bluetooth, pairing"),
            category: "connectivity",
            noFill: true
        },
        {
            label: Tr.tr("Audio"),
            icon: "volume_up",
            description: Tr.tr("App volumes, sound devices"),
            category: "connectivity"
        },

        // System
        {
            label: Tr.tr("Setup"),
            icon: "auto_fix_high",
            description: Tr.tr("Bar, apps, launcher, dashboard, lock - answer a few questions"),
            category: "system"
        },
        {
            label: Tr.tr("Windows"),
            icon: "desktop_windows",
            description: Tr.tr("Taskbar, startup, lock, wallpaper folder, config files"),
            category: "system"
        },
        {
            label: Tr.tr("Shortcuts"),
            icon: "keyboard",
            description: Tr.tr("Keys that open apps, folders and Caelestia's panels"),
            category: "system"
        },
        {
            label: Tr.tr("Hyprland"),
            icon: "dashboard",
            description: Tr.tr("Tile windows like Hyprland, from your hyprland.conf"),
            category: "system"
        },
        {
            label: Tr.tr("All settings"),
            icon: "tune",
            description: Tr.tr("Every option in shell.json and shell-tokens.json"),
            category: "system"
        },

        // Shell
        {
            label: Tr.tr("Panels"),
            icon: "dock_to_bottom",
            description: Tr.tr("Dashboard, taskbar, launcher, sidebar"),
            category: "shell"
        },
        {
            label: Tr.tr("Apps"),
            icon: "apps",
            description: Tr.tr("Default apps, favourites, hidden apps"),
            category: "shell"
        },
        {
            label: Tr.tr("Services"),
            icon: "build",
            description: Tr.tr("Poll intervals, lyrics backend"),
            category: "shell"
        },
        {
            label: Tr.tr("Language & region"),
            icon: "globe",
            description: Tr.tr("UI language, weather location, display units"),
            category: "shell"
        },

        // About
        {
            label: Tr.tr("About"),
            icon: "info",
            description: Tr.tr("System information, credits"),
            category: "about"
        },
    ]
}
