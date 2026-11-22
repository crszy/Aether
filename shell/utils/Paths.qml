pragma Singleton

import QtQuick
import Quickshell
import Caelestia
import Caelestia.Config

Singleton {
    id: root

    readonly property string home: (Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").replace(/\\/g, "/")
    readonly property string pictures: Quickshell.env("XDG_PICTURES_DIR") || `${home}/Pictures`
    readonly property string videos: Quickshell.env("XDG_VIDEOS_DIR") || `${home}/Videos`

    readonly property string data: `${Quickshell.env("XDG_DATA_HOME") || `${home}/.local/share`}/caelestia`
    readonly property string state: `${Quickshell.env("XDG_STATE_HOME") || `${home}/.local/state`}/caelestia`
    readonly property string cache: `${Quickshell.env("XDG_CACHE_HOME") || `${home}/.cache`}/caelestia`
    readonly property string config: `${Quickshell.env("XDG_CONFIG_HOME") || `${home}/.config`}/caelestia`
    // Windows: where Caelestia.Config keeps shell.json / shell-tokens.json / monitors (Qt's GenericConfigLocation)
    readonly property string appConfig: `${(Quickshell.env("LOCALAPPDATA") || `${home}/AppData/Local`).replace(/\\/g, "/")}/caelestia`

    readonly property string imagecache: `${cache}/imagecache`
    readonly property string notifimagecache: `${imagecache}/notifs`
    readonly property string wallsdir: Quickshell.env("CAELESTIA_WALLPAPERS_DIR") || absolutePath(GlobalConfig.paths.wallpaperDir)
    readonly property string recsdir: Quickshell.env("CAELESTIA_RECORDINGS_DIR") || `${videos}/Recordings`
    readonly property string libdir: Quickshell.env("CAELESTIA_LIB_DIR") || "/usr/lib/caelestia"

    function toLocalFile(path: url): string {
        path = Qt.resolvedUrl(path);
        return path.toString() ? CUtils.toLocalFile(path) : "";
    }

    function absolutePath(path: string): string {
        path = path.replace(/~|(\$({?)HOME(}?))+/, home).replace(/\\/g, "/");
        if (/^[A-Za-z]:\//.test(path))   // Windows drive path: already local
            return path;
        return toLocalFile(path);
    }

    function shortenHome(path: string): string {
        return path.replace(home, "~");
    }
}
