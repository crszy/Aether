pragma Singleton
import QtQuick
import qs.services as Cae

// GNOME Shell 48 / libadwaita dark colours and sizes (Debian 13 ships GNOME 48). One place to retune the look.
QtObject {
    property color accent: "#3584e4"
    // Liquid Glass option (windows.json "glass": "liquid"): panels become glass over the wallpaper below them
    readonly property bool glass: Cae.WinSettings.glass === "liquid"
    property string distroWallpaper: ""   // the distro's own picture, set by DistroHost
    readonly property string wallpaper: Cae.WinSettings.distroWallpaper ? "file:///" + Cae.WinSettings.distroWallpaper : distroWallpaper
    readonly property color panel: "#000000"
    readonly property color panelText: "#ffffff"
    readonly property color panelHover: Qt.rgba(1, 1, 1, 0.15)
    readonly property color popover: "#2e2e32"
    readonly property color card: "#3a3a3f"
    readonly property color cardHover: "#45454b"
    readonly property color text: "#ffffff"
    readonly property color dim: Qt.rgba(1, 1, 1, 0.6)
    readonly property color border: Qt.rgba(1, 1, 1, 0.08)
    readonly property color shadow: Qt.rgba(0, 0, 0, 0.45)
    // Adwaita Sans (GNOME 48) / Cantarell when installed, else Windows' own UI font
    readonly property string font: Qt.fontFamilies().indexOf("Adwaita Sans") >= 0 ? "Adwaita Sans"
        : Qt.fontFamilies().indexOf("Cantarell") >= 0 ? "Cantarell" : "Segoe UI Variable Text"
    readonly property string icons: "Material Symbols Rounded"
    readonly property int barHeight: 32
    readonly property int menuRadius: 22
    readonly property int animMs: Cae.WinSettings.distroReduceMotion ? 0 : 200
}
