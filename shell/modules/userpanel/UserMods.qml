pragma ComponentBehavior: Bound

// Your mods: ~/.config/caelestia/user/mods/<Name>.qml - code that runs inside Aether, like Windhawk mods do for
// Windows apps. A mod can react to anything Aether sees (a window opening, the workspace changing, music starting,
// the time of day, battery, volume...) and change anything (settings, skin, wallpaper, colour scheme, windows,
// panels, launcher commands, programs). No dev build: save the file and it reloads.
//
// A mod is any QML object (QtObject, Item, Timer, Connections ...). If it declares `property var aether`, it gets
// the helpers below. It can also `import qs.services` (Audio, Players, Time, Wallpapers, WinSettings, Colours ...)
// and `import Quickshell.Hyprland` (Hyprland.focusedWorkspace, Hyprland.dispatch(), Connections on rawEvent).
//
//   aether.set("key", value) / aether.get("key")   any windows.json setting (mixBar, aetherSkin, lockBlur ...)
//   aether.skin("TokyoRGB")                        switch skin ("" = none)
//   aether.wallpaper("C:/path.jpg")                set the wallpaper (colours follow)
//   aether.scheme("dynamic")                       colour scheme (caelestia scheme set -n ...)
//   aether.dispatch("workspace 2")                 an AetherWM / Hyprland dispatcher
//   aether.panel("profile") / aether.closePanel()  your panels
//   aether.run("profile")                          one of your launcher commands (commands.json)
//   aether.exec(["wt.exe"]) / aether.open(url)     start a program / open a link
//   aether.notify("Title", "Text", "icon")         a toast
//   aether.ipc("lock", "lock", [])                 any Aether IPC call
//
// Turn one off: rename it to _<Name>.qml, or `qs ipc call user mod <Name> off`. A broken mod only logs its error.
import QtQuick
import Quickshell
import Quickshell.Hyprland
import Quickshell.Io
import qs.services

Scope {
    id: host

    // the helpers every mod gets as `aether`
    QtObject {
        id: api

        function set(key: string, value: var): void {
            if (key in WinSettings)
                WinSettings[key] = value;
            else
                console.warn("[mods] no setting called", key);
        }
        function get(key: string): var {
            return WinSettings[key];
        }
        function skin(name: string): void {
            WinSettings.aetherSkin = name ?? "";
        }
        function wallpaper(path: string): void {
            Wallpapers.setWallpaper(String(path).replace(/\\/g, "/"));
        }
        function scheme(name: string): void {
            Quickshell.execDetached(["caelestia", "scheme", "set", "-n", String(name)]);
        }
        function dispatch(what: string): void {
            Hyprland.dispatch(String(what));
        }
        function panel(name: string): void {
            UserExt.openPanel(name);
        }
        function closePanel(): void {
            UserExt.closePanel();
        }
        function run(name: string): string {
            return UserExt.run(name);
        }
        function exec(argv: var): void {
            Quickshell.execDetached(Array.isArray(argv) ? argv.map(String) : ["cmd.exe", "/c", String(argv)]);
        }
        function open(url: string): void {
            Qt.openUrlExternally(String(url));
        }
        function notify(title: string, text: string, icon: string): void {
            WinSettings.ipc("aether", "info", [String(title ?? ""), String(text ?? ""), String(icon || "extension")]);
        }
        function ipc(target: string, fn: string, args: var): void {
            WinSettings.ipc(String(target), String(fn), (args ?? []).map(String));
        }
    }

    Variants {
        model: UserExt.modNames.filter(n => !(WinSettings.modsOff ?? []).includes(n))

        Scope {
            id: mod

            required property string modelData
            property QtObject instance: null
            readonly property string path: UserExt.filePath("mods", modelData)

            function clear(): void {
                if (mod.instance) {
                    mod.instance.destroy();
                    mod.instance = null;
                }
            }

            function build(text: string): void {
                mod.clear();
                try {
                    const obj = Qt.createQmlObject(text, mod, `file:///${mod.path}`);
                    if (obj && "aether" in obj)
                        obj.aether = api;
                    mod.instance = obj;
                    console.info("[mods] loaded", mod.modelData);
                } catch (e) {
                    const errs = e?.qmlErrors ?? [];
                    console.warn("[mods]", mod.modelData, errs.length ? errs.map(er => `line ${er.lineNumber}:${er.columnNumber}: ${er.message}`).join("; ") : String(e));
                }
            }

            Component.onDestruction: clear()

            FileView {
                path: mod.path
                watchChanges: true
                printErrors: false
                onFileChanged: reload()
                onLoaded: mod.build(text())
            }
        }
    }
}
