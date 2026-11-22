pragma Singleton
import "."
import QtQuick
import Quickshell

QtObject {
    readonly property string home: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/"))
    readonly property string shellDir: Quickshell.env("ZENITH_ROOT") || (home + "/.config/quickshell")
    readonly property string scriptsDir: shellDir + "/scripts"
    readonly property string configDir: home + "/.config"
    readonly property string cacheDir: (Quickshell.env("XDG_CACHE_HOME") || (home + "/.cache")) + "/zenith"
    // Runtime state the shell writes (launch counts, focus sessions, fetched
    // holidays). Kept out of the repo so a clone stays clean and a pull never
    // conflicts on generated data.
    readonly property string stateDir: (Quickshell.env("XDG_STATE_HOME") || (home + "/.local/state")) + "/zenith"
}
