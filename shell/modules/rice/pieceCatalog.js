.pragma library
// Mix & match pieces offered as buttons in the Mix & match panel (see dotfiles/MIXING-PLAN.md).
// Bar widgets by shell, as "shell:Widget" ids for windows.json "barPieces" (horizontal-capable ones).

var barWidgets = {
    "DMS": ["AudioVisualization", "Battery", "CapsLockIndicator", "ClipboardButton", "Clock", "ColorPicker",
        "ControlCenterButton", "CpuMonitor", "CpuTemperature", "DiskUsage", "FocusedApp", "GpuTemperature",
        "IdleInhibitor", "KeyboardLayoutName", "LauncherButton", "Media", "NetworkMonitor", "NotepadButton",
        "NotificationCenterButton", "PowerMenuButton", "PrivacyIndicator", "RamMonitor", "RunningApps",
        "SystemTrayBar", "SystemUpdate", "Vpn", "Weather", "WorkspaceSwitcher"].map(function (w) { return "dms:" + w; }),
    "Noctalia": ["ActiveWindow", "AudioVisualizer", "Battery", "Bluetooth", "Brightness", "Clock", "ControlCenter",
        "DarkMode", "KeepAwake", "KeyboardLayout", "Launcher", "LockKeys", "MediaMini", "Microphone", "Network",
        "NightLight", "NotificationHistory", "PowerProfile", "SessionMenu", "Settings", "SystemMonitor", "Taskbar",
        "Tray", "VPN", "Volume", "WallpaperSelector", "Workspace"].map(function (w) { return "noctalia:" + w; }),
    "Zenith": ["Workspaces", "Tray", "Resources", "Network", "QuickSettings"].map(function (w) { return "zenith:" + w; })
};

var lockPieces = ["caelestia", "dms", "end4", "noctalia"];
var lockWallpaper = ["auto", "caelestia", "own"];
