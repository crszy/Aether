pragma Singleton
// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows helper added by dotfiles/serp-port.js: what serp did with bash, qs_manager.sh, rm, touch and wl-copy.
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as WinFs

Singleton {
    id: root

    // serp's "main" IpcHandler (Main.qml sets it): qs_manager.sh only forwarded to it
    property var main: null

    // qs_manager.sh <action> [target] [sub] -> Main's handleCommand
    function qsManager(args: var): void {
        const a = (Array.isArray(args) ? args : String(args).trim().split(/\s+/)).map(x => String(x ?? ""));
        let action = a[0] || "", target = a[1] || "", sub = a[2] || "";
        if (/^[0-9]+$/.test(action) || action === "workspace" || action === "reload")
            action = "close";   // workspaces belong to Windows / AetherWM
        if (action === "guide" || action === "settings") {
            sub = target;
            target = "settings";
            action = "open";
        }
        if (!root.main) {
            console.warn("serp: Main is not loaded, cannot run", a.join(" "));
            return;
        }
        root.main.handleCommand(action, target, sub);
    }

    // serp's installer downloads Symbols Nerd Font; on Windows use it if present, else any installed Nerd Font
    function iconFontUrl(dataDir: string): string {
        const bundled = dataDir + "/assets/fonts/SymbolsNerdFontMono-Regular.ttf";
        const s = WinFs.FsUtils.statPath(bundled);
        if (s && s.path)
            return root.url(bundled);
        const home = Quickshell.env("LOCALAPPDATA") || "";
        const found = WinFs.FsUtils.findFiles([home.split("\\").join("/") + "/Microsoft/Windows/Fonts", "C:/Windows/Fonts"],
            ["SymbolsNerdFont*.ttf", "*NerdFontMono-Regular.ttf", "*NerdFont-Regular.ttf"], 1, 1);
        return found && found.length ? root.url(found[0]) : "";
    }

    function url(x: var): string {
        const s = String(x ?? "");
        if (/^[a-z][a-z0-9+.-]*:/i.test(s) && !/^[A-Za-z]:[\\/]/.test(s))
            return s;
        return "file:///" + s.replace(/^\/+/, "").split("\\").join("/");
    }

    function rm(p: var): void {
        if (p)
            WinFs.FsUtils.removeFile(String(p));
    }

    function touch(p: var): void {
        if (!p)
            return;
        const s = String(p);
        WinFs.FsUtils.makePath(s.slice(0, s.lastIndexOf("/")));
        writer.path = s;
        writer.setText("");
    }

    function writeFile(p: string, text: string): void {
        WinFs.FsUtils.makePath(p.slice(0, p.lastIndexOf("/")));
        writer.path = p;
        writer.setText(text);
    }

    FileView {
        id: writer
        blockWrites: true
        atomicWrites: true
        printErrors: false
        preload: false
    }
}
