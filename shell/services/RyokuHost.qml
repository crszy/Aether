// Windows (Aether): runs Ryoku (dotfiles/ryoku-port.js -> <package>/ryoku) while Mix & match picks Ryoku for the bar,
// the way Ryoku runs itself on Linux: its daemon (ryoku-shell daemon, built for Windows by ryoku-go-port.js) starts
// and watches the shell, and Ryogami's daemon runs next to it. Without the built programs (<package>/ryoku/bin) the
// shell is started directly. QML modules come from QML_IMPORT_PATH, Ryoku's programs, Aether's qsw.exe and Git's
// Unix tools go first on PATH, and everything ends with Aether (Process is bound to the shell's lifetime).
pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as WinFs

Singleton {
    id: root

    // Ryoku runs while ANY of its parts is picked in Mix & match; Ryoku shows only those parts (aether-parts.json ->
    // AetherCompat.AetherParts in Ryoku). Its lock (qylock) is its own instance and needs none of this.
    readonly property var parts: ({
        "bar": WinSettings.mixBar === "ryoku",
        "wallpaper": WinSettings.mixWallpaper === "ryoku",
        "widgets": WinSettings.mixDesktopWidgets === "ryoku",
        "dock": WinSettings.mixDock === "ryoku",
        "notifs": WinSettings.mixNotifs === "ryoku",
        "osd": WinSettings.mixOsd === "ryoku",
        "launcher": WinSettings.mixLauncher === "ryoku"
    })
    readonly property bool wanted: Object.values(parts).some(on => on)
    onPartsChanged: writeParts()
    function writeParts(): void {
        if (!WinSettings.loaded)
            return;
        WinFs.FsUtils.makePath(root.runDir);
        partsFile.setText(JSON.stringify(root.parts));
    }
    Connections {
        target: WinSettings
        function onLoadedChanged(): void { root.writeParts(); }
    }
    FileView {
        id: partsFile

        path: `${root.runDir}/aether-parts.json`
        printErrors: false
    }
    readonly property string pkg: WinSettings.packageDir
    readonly property string ryokuDir: pkg ? `${pkg}/ryoku` : ""
    readonly property bool daemons: ryokuDir !== "" && WinFs.FsUtils.classify(`${ryokuDir}/bin/ryoku-shell.exe`) === "file"
    readonly property string runDir: `${String(Quickshell.env("LOCALAPPDATA") || "").replace(/\\/g, "/")}/AetherShell/ryoku-run`

    readonly property var env: ({
        "QML_IMPORT_PATH": root.ryokuDir,
        "QML_XHR_ALLOW_FILE_READ": "1",
        "PATH": `${root.ryokuDir}/bin;${root.pkg}/qs;C:\\Program Files\\Git\\usr\\bin;` + (Quickshell.env("PATH") || ""),
        // where Ryoku's apps and Aether's own qs.exe / shell are (AetherCompat.RyokuCompat and the daemon use them)
        "RYOKU_DIR": root.ryokuDir,
        "RYOKU_SHELL": WinSettings.ryokuShell,   // your editable copy (~/.config/quickshell/ryoku) when it exists
        "AETHER_QS_DIR": `${root.pkg}/qs`,
        "AETHER_SHELL": Quickshell.shellPath("shell.qml"),
        // Ryoku's sockets live here (named pipes \\.\pipe\<path> on Windows); its Hyprland provider drives Aether tiling
        "XDG_RUNTIME_DIR": root.runDir,
        "RYOKU_WM": "hyprland",
        // Ryogami's wallpaper picker (Ryoku.Ryogami plugin, built by ryogami-picker-build.bat)
        "RYOGAMI_SHELL_QML": `${root.ryokuDir}/apps/ryogami/shell.qml`,
        // Ryoku builds its paths from HOME, which Windows does not always set
        "HOME": Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || ""
    })

    // switched away from Ryoku: stopping the Process alone left Ryoku's window running - Quickshell's own kill
    // ends that instance cleanly
    Component.onCompleted: if (WinSettings.loaded && !root.wanted && root.ryokuDir !== "")
        Quickshell.execDetached(["taskkill.exe", "/F", "/T", "/IM", "ryoku-shell.exe", "/IM", "ryogami.exe", "/IM", "ryoku-wm-hyprland.exe", "/IM", "ryoku-rashin.exe"])

    onWantedChanged: {
        if (!root.wanted)
            root.synced = false;
        if (!root.wanted && root.ryokuDir !== "") {
            // the daemon first (it would start the shell again), then its helpers, then the shell
            Quickshell.execDetached(["taskkill.exe", "/F", "/T", "/IM", "ryoku-shell.exe", "/IM", "ryogami.exe", "/IM", "ryoku-wm-hyprland.exe", "/IM", "ryoku-rashin.exe"]);
            Quickshell.execDetached([`${root.pkg}/qs/qs.exe`, "kill", "-p", WinSettings.ryokuShell]);
        }
    }

    // Ryoku's lock screen (qylock, ryoku/lockscreen): the Windows password through Aether's check, its own instance
    function lock(): bool {
        const shell = `${root.ryokuDir}/lockscreen/lock_shell.qml`;
        if (!root.ryokuDir || WinFs.FsUtils.classify(shell) !== "file")
            return false;
        Quickshell.execDetached({
            command: [`${root.pkg}/qs/qsw.exe`, "-p", shell],
            environment: { "QML_IMPORT_PATH": `${root.ryokuDir}/lockscreen/imports;${root.ryokuDir}`, "QML_XHR_ALLOW_FILE_READ": "1" }
        });
        return true;
    }

    Process {   // the daemon (it starts the shell itself), or the shell alone
        running: root.wanted && root.ryokuDir !== ""
        command: root.daemons ? [`${root.ryokuDir}/bin/ryoku-shell.exe`, "daemon"] : [`${root.pkg}/qs/qsw.exe`, "-p", WinSettings.ryokuShell]
        environment: root.env
        // drained: a Process with no parser leaves the child's stdout/stderr pipe unread on Windows, and the Go daemon
        // blocked on its first log lines before it ever started the shell (no Ryoku bar or wallpaper, 2026-11-15)
        stdout: SplitParser {}
        stderr: SplitParser { onRead: line => console.info("[ryoku-daemon]", line) }
    }

    Process {   // Rashin (Ryoku's AI console) - serves only if the user turned it on, like its Linux service
        running: root.wanted && root.daemons && WinFs.FsUtils.classify(`${root.ryokuDir}/bin/ryoku-rashin.exe`) === "file"
        command: [`${root.ryokuDir}/bin/ryoku-rashin.exe`, "serve", "--if-enabled"]
        environment: root.env
        // drained: a Process with no parser leaves the child's stdout/stderr pipe unread on Windows, and the Go daemon
        // blocked on its first log lines before it ever started the shell (no Ryoku bar or wallpaper, 2026-11-15)
        stdout: SplitParser {}
        stderr: SplitParser {}
    }

    Process {   // Ryogami's wallpaper daemon
        id: ryogami

        running: root.wanted && root.daemons && WinFs.FsUtils.classify(`${root.ryokuDir}/bin/ryogami.exe`) === "file"
        command: [`${root.ryokuDir}/bin/ryogami.exe`, "daemon"]
        environment: root.env
        // drained: a Process with no parser leaves the child's stdout/stderr pipe unread on Windows, and the Go daemon
        // blocked on its first log lines before it ever started the shell (no Ryoku bar or wallpaper, 2026-11-15)
        stdout: SplitParser {}
        stderr: SplitParser {}
    }

    // ---- one wallpaper for both: Aether's goes to Ryogami (which Ryoku's desktop paints), and one picked in Ryogami
    // comes back to Aether (its colours follow, and switching shells keeps it)
    readonly property string aetherWall: Wallpapers.actualCurrent
    property string ryogamiWall: ""
    // Ryogami's first frames are its own last pick: they never overwrite Aether's until Aether's has been sent
    property bool synced: false

    function norm(p: string): string {
        return String(p || "").replace(/\\/g, "/").toLowerCase();
    }

    function pushWall(): void {
        if (!ryogami.running || !root.aetherWall)
            return;
        root.synced = true;
        if (norm(root.aetherWall) === norm(root.ryogamiWall))
            return;
        wallSet.command = [`${root.ryokuDir}/bin/ryogami.exe`, "wallpaper", "set", String(root.aetherWall).replace(/\//g, "\\")];
        wallSet.running = true;
    }

    onAetherWallChanged: pushWall()

    Process {
        id: wallSet

        environment: root.env
        // drained: a Process with no parser leaves the child's stdout/stderr pipe unread on Windows, and the Go daemon
        // blocked on its first log lines before it ever started the shell (no Ryoku bar or wallpaper, 2026-11-15)
        stdout: SplitParser {}
        stderr: SplitParser {}
    }

    Timer {   // Ryogami restores its own last pick at start: Aether's wallpaper wins once its socket is up
        running: ryogami.running
        interval: 3000
        onTriggered: root.pushWall()
    }

    Socket {   // Ryogami's wallpaper topic (a named pipe on Windows)
        id: wallTopic

        path: `${root.runDir}/ryogami.sock`
        connected: ryogami.running
        onConnectedChanged: if (connected) write("subscribe wallpaper\n")
        parser: SplitParser {
            onRead: line => {
                try {
                    const frame = JSON.parse(line);
                    const path = frame.default ? String(frame.default.path || "") : "";
                    if (!path)
                        return;
                    root.ryogamiWall = path;
                    if (root.synced && root.norm(path) !== root.norm(root.aetherWall) && !(frame.default.live))
                        Wallpapers.setWallpaper(path.replace(/\\/g, "/"));
                } catch (e) {}
            }
        }
    }

    Timer {   // the daemon may come up after the first try
        running: ryogami.running && !wallTopic.connected
        interval: 2000
        repeat: true
        onTriggered: wallTopic.connected = true
    }
}
