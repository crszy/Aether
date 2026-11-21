pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as WinFs
import Caelestia
import qs.services as Cae
import qs.modules.nexus as CaeNexus
import qs.end4
import qs.end4.modules.common

// What end-4's buttons do on Windows. Upstream they run Linux tools (qs -p settings.qml, hyprpicker, wpctl,
// grim, ydotool, loginctl...); end4-port.js points every one of them here instead.
Singleton {
    id: root

    // call one of Caelestia's IPC targets (the same thing its keyboard shortcuts do)
    function caelestiaIpc(target, fn, args) {
        Cae.WinSettings.ipc(target, fn, args ?? []);
    }

    // open a program, a file, a link or an ms-settings: page the way the Run box does
    function open(what) {
        if (!what)
            return;
        Quickshell.execDetached(["cmd.exe", "/c", "start", "", what]);
    }

    // launcher entries that want a terminal / ">" commands
    function runInTerminal(command) {
        Quickshell.execDetached(["cmd.exe", "/c", "start", "", "wt.exe", "cmd.exe", "/k"].concat(command ?? []));
    }
    function runCommand(command) {
        if (command)
            Quickshell.execDetached(["cmd.exe", "/c", "start", "", "cmd.exe", "/k", command]);
    }

    // Settings: Aether's global settings (the style set in Settings > Mix & match)
    function openSettings() {
        GlobalStates.sidebarRightOpen = false;
        CaeNexus.WindowFactory.create();
    }

    // screen snip: Caelestia's area picker (copies + saves)
    function screenshot() {
        GlobalStates.sidebarRightOpen = false;
        root.caelestiaIpc("picker", "open");
    }

    function screenshotClip() {
        root.caelestiaIpc("picker", "openClip");
    }

    // screen recording: Caelestia's recorder (press again to stop)
    function record(withSound) {
        if (Cae.Recorder.running)
            Cae.Recorder.stop();
        else
            Cae.Recorder.start(withSound ? ["-s"] : []);
    }

    // colour picker: PowerToys' one if it is installed, else Caelestia's picker in clipboard mode
    function colorPicker() {
        const local = Quickshell.env("LOCALAPPDATA") + "/PowerToys/PowerToys.ColorPickerUI.exe";
        const global = Quickshell.env("ProgramFiles") + "/PowerToys/PowerToys.ColorPickerUI.exe";
        const exe = [local, global].find(p => WinFs.FsUtils.classify(p) === "file");
        if (exe)
            Quickshell.execDetached([exe]);
        else
            root.screenshotClip();
    }

    // on-screen keyboard: Windows' own (end-4's types through ydotool, which Windows does not have)
    function toggleOsk() {
        oskCheck.running = true;
    }

    Process {
        id: oskCheck
        command: ["tasklist.exe", "/nh", "/fi", "IMAGENAME eq osk.exe"]
        stdout: StdioCollector {
            onStreamFinished: {
                if (text.toLowerCase().includes("osk.exe")) {
                    Quickshell.execDetached(["taskkill.exe", "/im", "osk.exe"]);
                    GlobalStates.oskOpen = false;
                } else {
                    // osk.exe asks for UI access, so it has to go through ShellExecute (explorer), not CreateProcess
                    Quickshell.execDetached(["explorer.exe", "C:\\Windows\\System32\\osk.exe"]);
                    GlobalStates.oskOpen = true;
                }
            }
        }
    }

    function toggleMic() {
        const src = Cae.Audio.source;
        if (src?.audio)
            src.audio.muted = !src.audio.muted;
    }

    function setDark(dark) {
        Cae.Colours.setMode(dark ? "dark" : "light");
    }

    // end-4's "notify-send" calls: a Caelestia toast
    function notifySend(args) {
        const texts = (args ?? []).filter(a => typeof a === "string" && !a.startsWith("-"));
        Toaster.toast(texts[0] ?? "end-4", texts[1] ?? "", "info", Toast.Info);
    }

    // game mode: Caelestia's (no animations / blur while it is on)
    function gameMode(on) {
        root.caelestiaIpc("gameMode", on ? "enable" : "disable");
    }

    // AI chat "save code": writes the file (no bash echo)
    function saveText(filePath, text) {
        saveFile.path = filePath;
        saveFile.setText(text);
    }

    FileView {
        id: saveFile
        blockWrites: false
        atomicWrites: true
    }

    // image download (Booru): curl.exe, part of Windows
    function download(url, folder, name, userAgent) {
        const cmd = ["curl.exe", "-sL", "--create-dirs", "-o", `${folder}/${name}`];
        if (userAgent)
            cmd.push("-A", userAgent);
        cmd.push(url);
        Quickshell.execDetached(cmd);
    }

    // end-4's own IPC targets live in this same shell
    function end4Ipc(target, fn) {
        if (target === "sidebarLeft")
            GlobalStates.sidebarLeftOpen = !GlobalStates.sidebarLeftOpen;
        else if (target === "overview")
            GlobalStates.overviewOpen = !GlobalStates.overviewOpen;
        else if (target === "region")
            root.screenshot();
    }

    // the same actions for keyboard shortcuts: qs ipc call end4:actions <fn>
    IpcHandler {
        target: "end4:actions"

        function settings(): void {
            root.openSettings();
        }
        function keyboard(): void {
            root.toggleOsk();
        }
        function screenshot(): void {
            root.screenshot();
        }
        function record(): void {
            root.record(false);
        }
        function colorPicker(): void {
            root.colorPicker();
        }
        function mic(): void {
            root.toggleMic();
        }
        function darkMode(): void {
            root.setDark(!Appearance.m3colors.darkmode);
        }
    }

    // session (Windows' own commands; the lock is the lock chosen in Mix & match)
    function lock() {
        root.caelestiaIpc("lock", "lock");
    }
    function suspend() {
        Quickshell.execDetached(["rundll32.exe", "powrprof.dll,SetSuspendState", "0,1,0"]);
    }
    function hibernate() {
        Quickshell.execDetached(["shutdown.exe", "/h"]);
    }
    function logout() {
        Quickshell.execDetached(["shutdown.exe", "/l"]);
    }
    function poweroff() {
        Quickshell.execDetached(["shutdown.exe", "/s", "/t", "0"]);
    }
    function reboot() {
        Quickshell.execDetached(["shutdown.exe", "/r", "/t", "0"]);
    }
    function rebootToFirmware() {
        Quickshell.execDetached(["shutdown.exe", "/r", "/fw", "/t", "0"]);
    }
}
