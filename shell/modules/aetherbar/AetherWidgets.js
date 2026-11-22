.pragma library
// Aether shell (AGPL-3.0-or-later). The widgets the Aether bar can show, and where each idea comes from.
// Settings > Bar > Aether bar lists these in its "Add a widget" picker, grouped by `from`.
var list = [
    { id: "logo", name: "Aether logo", icon: "auto_awesome", from: "Aether", desc: "Opens the dashboard" },
    { id: "workspaces", name: "Workspaces", icon: "grid_view", from: "Ryoku", desc: "Your Hyprland workspaces; scroll to switch" },
    { id: "clock", name: "Clock", icon: "schedule", from: "Ryoku", desc: "Time and date; click for the dashboard" },
    { id: "music", name: "Now playing", icon: "music_note", from: "Ryoku", desc: "The song playing; click for music and lyrics" },
    { id: "sysmon", name: "System monitor", icon: "memory", from: "Ryoku", desc: "CPU and memory use" },
    { id: "tray", name: "Tray", icon: "more_horiz", from: "Ryoku", desc: "Apps in the notification area" },
    { id: "volume", name: "Volume", icon: "volume_up", from: "Ryoku", desc: "Scroll to change, click to mute" },
    { id: "network", name: "Network", icon: "wifi", from: "Ryoku", desc: "Online or not" },
    { id: "battery", name: "Battery", icon: "battery_full", from: "Ryoku", desc: "Only shows on a laptop" },
    { id: "notifs", name: "Notifications", icon: "notifications", from: "Ryoku", desc: "Count; right-click for do not disturb" },
    { id: "power", name: "Power", icon: "power_settings_new", from: "Ryoku", desc: "Lock, sign out, restart, shut down" },
    { id: "window", name: "Active window", icon: "web_asset", from: "end-4", desc: "The focused window's title" },
    { id: "weather", name: "Weather", icon: "partly_cloudy_day", from: "end-4", desc: "Temperature and sky now" },
    { id: "media", name: "Media buttons", icon: "skip_next", from: "end-4", desc: "Previous, play/pause, next" },
    { id: "date", name: "Date", icon: "calendar_today", from: "serpantinum", desc: "The day and date on their own" },
    { id: "keyboard", name: "Keyboard layout", icon: "keyboard", from: "DarkShadowShell", desc: "Current layout and caps lock" },
    { id: "spacer", name: "Divider", icon: "more_vert", from: "DarkShadowShell", desc: "A thin line between widgets" }
];
var sources = ["Aether", "Ryoku", "end-4", "serpantinum", "DarkShadowShell"];
function byId(id) {
    for (var i = 0; i < list.length; i++)
        if (list[i].id === id)
            return list[i];
    return null;
}
