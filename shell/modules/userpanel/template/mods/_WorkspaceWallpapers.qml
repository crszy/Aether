// Mod: WorkspaceWallpapers - every workspace gets its own wallpaper (and the colours follow it).
// Switch it on: rename to WorkspaceWallpapers.qml, then put your pictures below (workspace number: picture).
// Works with AetherWM (Hyprland mode): it listens to the window manager's workspace events.
import QtQuick
import Quickshell.Hyprland

Connections {
    property var aether
    readonly property var walls: ({
        "1": "C:/Users/you/Pictures/Wallpapers/one.jpg",
        "2": "C:/Users/you/Pictures/Wallpapers/two.jpg",
        "3": "C:/Users/you/Pictures/Wallpapers/three.jpg"
    })

    target: Hyprland

    function onFocusedWorkspaceChanged(): void {
        const id = String(Hyprland.focusedWorkspace?.id ?? "");
        if (aether && walls[id])
            aether.wallpaper(walls[id]);
    }
}
