pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import "../Settings"

QtObject {
    id: variables

    // =====================================================
    // ============ CENTRAL RECURRING TASK SYSTEM ==========
    // =====================================================
    readonly property int fastInterval: TaskSettings.fastPollInterval
    readonly property int mediumInterval: TaskSettings.mediumPollInterval
    readonly property int slowInterval: TaskSettings.slowPollInterval
    readonly property int lazyInterval: TaskSettings.lazyPollInterval
    readonly property int idleInterval: TaskSettings.idlePollInterval

    // Adaptive Task Scheduler Flags
    property bool quickSettingsOpen: false
    property bool controlCenterOpen: false
    property bool activeMenuOpen: quickSettingsOpen || controlCenterOpen


    // Distro & OS Metadata
    property string distroId: "nixos"
    property string distroName: "NixOS"
    property string distroVersion: ""
    property string kernelVersion: ""
    property string hostname: "desktop"
    property bool isNixOS: true
    property bool isArch: false

    // User & System Metadata
    property string user: Quickshell.env("USER") || "zaeem"
    property string homeDir: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) || "/tmp"

    // Location, Country, Locale & Screen Resolution
    property string timezone: "UTC"
    property string language: "en_US"
    property string countryCode: "PK"
    property string countryName: "Pakistan"
    property int screenWidth: 1920
    property int screenHeight: 1080
    property int refreshRate: 60
    property string resolution: "1920x1080"

    // Pre-computed, verified Icon Base Directories
    property var iconBases: [
        "/etc/profiles/per-user/" + user + "/share/icons",
        "/run/current-system/sw/share/icons",
        homeDir + "/.local/share/icons",
        homeDir + "/.icons",
        "/usr/share/icons"
    ]

    // Icon theme directories verified to exist, filled by detect_env.sh.
    // Until then (or if detection fails) IconsFetcher falls back to
    // iconBases x themeSubPaths.
    property var iconDirs: []

    // Pre-combined high-frequency theme subpaths
    property var themeSubPaths: [
        "/Reversal/status@2x/32/",
        "/Reversal/status@2x/22/",
        "/Reversal/status/32/",
        "/Reversal/status/22/",
        "/Reversal/status/scalable/",
        "/Reversal/apps/scalable/",
        "/Reversal/apps/48/",
        "/Reversal-dark/status@2x/32/",
        "/Reversal-dark/status@2x/22/",
        "/Reversal-dark/status/scalable/",
        "/Reversal-dark/apps/scalable/",
        "/breeze/status/22@2x/",
        "/breeze/status/22/",
        "/breeze/status/scalable/",
        "/breeze/apps/scalable/",
        "/breeze/apps/48/",
        "/breeze-dark/status/22/",
        "/breeze-dark/status/scalable/",
        "/hicolor/scalable/status/",
        "/hicolor/scalable/apps/",
        "/hicolor/48x48/status/",
        "/hicolor/48x48/apps/",
        "/Papirus/48x48/apps/",
        "/Papirus/scalable/apps/",
        "/Adwaita/scalable/apps/",
        "/"
    ]

    property Process envDetector: Process {
        command: ["bash", PathSettings.scriptsDir + "/detect_env.sh"]
        running: true

        // detect_env.sh prints one pretty-printed JSON object. This was a
        // SplitParser, which hands over one *line* at a time, so JSON.parse
        // failed on every line and none of the detected values ever landed;
        // the shell ran on the hardcoded defaults below without anyone
        // noticing, including a /usr/share/icons path that does not exist on
        // NixOS.
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    let parsed = JSON.parse(text);
                    if (parsed.distroId) variables.distroId = parsed.distroId;
                    if (parsed.distroName) variables.distroName = parsed.distroName;
                    if (parsed.distroVersion !== undefined) variables.distroVersion = parsed.distroVersion;
                    if (parsed.kernelVersion) variables.kernelVersion = parsed.kernelVersion;
                    if (parsed.hostname) variables.hostname = parsed.hostname;
                    if (parsed.isNixOS !== undefined) variables.isNixOS = parsed.isNixOS;
                    if (parsed.isArch !== undefined) variables.isArch = parsed.isArch;
                    if (parsed.user) variables.user = parsed.user;
                    if (parsed.homeDir) variables.homeDir = parsed.homeDir;
                    if (parsed.timezone) variables.timezone = parsed.timezone;
                    if (parsed.language) variables.language = parsed.language;
                    if (parsed.countryCode) variables.countryCode = parsed.countryCode;
                    if (parsed.countryName) variables.countryName = parsed.countryName;
                    if (parsed.screenWidth) variables.screenWidth = parsed.screenWidth;
                    if (parsed.screenHeight) variables.screenHeight = parsed.screenHeight;
                    if (parsed.refreshRate) variables.refreshRate = parsed.refreshRate;
                    if (parsed.resolution) variables.resolution = parsed.resolution;

                    if (parsed.iconBases && Array.isArray(parsed.iconBases) && parsed.iconBases.length > 0) {
                        variables.iconBases = parsed.iconBases.filter((v, i, a) => v && v !== "" && a.indexOf(v) === i);
                    }
                    if (Array.isArray(parsed.iconDirs)) variables.iconDirs = parsed.iconDirs;
                } catch (e) {
                    // Fallback to default NixOS initialization if JSON parse error
                }
            }
        }
    }
}
