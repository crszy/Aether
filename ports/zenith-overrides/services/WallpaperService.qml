import QtQuick
import Quickshell
import qs.utils as CaeU
import "../Settings"

pragma Singleton

// Windows version of Zenith's WallpaperService: its picker lists Caelestia's wallpaper folder (Settings >
// paths.wallpaperDir, the one Caelestia's own picker uses) and ~/Videos/Animated. There is no thumbnail
// generator (python): the picker loads each picture itself at thumbnail size.
Item {
    id: root

    readonly property string home: PathSettings.home
    readonly property string wallpaperDir: "file:///" + String(CaeU.Paths.wallsdir).replace(/^file:\/+/, "")
    readonly property string animationDir: "file:///" + home + "/Videos/Animated"

    function generate() {}
}
