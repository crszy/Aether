// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
pragma Singleton
import QtQuick
import Quickshell.Windows as WinFs
import Quickshell

QtObject {
    id: root

    readonly property string serpantinumDir: (Quickshell.shellDir + "/serp/_data")
    readonly property string qsDir: (Quickshell.shellDir + "/serp")

    readonly property string home: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/"))
    readonly property string xdgRuntimeDir: ((Quickshell.env("TEMP") || "").split("\\").join("/"))

    readonly property string cacheDir: Quickshell.env("QS_CACHE_DIR") ? Quickshell.env("QS_CACHE_DIR") : (home + "/.cache/serpantinum")
    readonly property string stateDir: Quickshell.env("QS_STATE_DIR") ? Quickshell.env("QS_STATE_DIR") : (home + "/.local/state/serpantinum")
    readonly property string runDir: Quickshell.env("QS_RUN_DIR") ? Quickshell.env("QS_RUN_DIR") : ((xdgRuntimeDir !== "" ? xdgRuntimeDir : "/tmp") + "/serpantinum")
    readonly property string logDir: Quickshell.env("QS_LOG_DIR") ? Quickshell.env("QS_LOG_DIR") : (runDir + "/logs")

    function getCacheDir(widgetName) {
        if (!widgetName || widgetName === "serpantinum" || cacheDir.endsWith("/" + widgetName)) {
            WinFs.FsUtils.makePath(cacheDir);
            return cacheDir;
        }
        var envPath = Quickshell.env("QS_CACHE_" + widgetName.toUpperCase());
        var finalPath = envPath ? envPath : (cacheDir + "/" + widgetName);
        WinFs.FsUtils.makePath(finalPath);
        return finalPath;
    }

    function getStateDir(widgetName) {
        if (!widgetName || widgetName === "serpantinum" || stateDir.endsWith("/" + widgetName)) {
            WinFs.FsUtils.makePath(stateDir);
            return stateDir;
        }
        var envPath = Quickshell.env("QS_STATE_" + widgetName.toUpperCase());
        var finalPath = envPath ? envPath : (stateDir + "/" + widgetName);
        WinFs.FsUtils.makePath(finalPath);
        return finalPath;
    }

    function getRunDir(widgetName) {
        if (!widgetName || widgetName === "serpantinum" || runDir.endsWith("/" + widgetName)) {
            WinFs.FsUtils.makePath(runDir);
            return runDir;
        }
        var envPath = Quickshell.env("QS_RUN_" + widgetName.toUpperCase());
        var finalPath = envPath ? envPath : (runDir + "/" + widgetName);
        WinFs.FsUtils.makePath(finalPath);
        return finalPath;
    }

    function getLogDir(widgetName) {
        if (!widgetName || widgetName === "serpantinum" || logDir.endsWith("/" + widgetName)) {
            WinFs.FsUtils.makePath(logDir);
            return logDir;
        }
        var envPath = Quickshell.env("QS_LOG_" + widgetName.toUpperCase());
        var finalPath = envPath ? envPath : (logDir + "/" + widgetName);
        WinFs.FsUtils.makePath(finalPath);
        return finalPath;
    }
}
