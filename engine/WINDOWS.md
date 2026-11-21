# How the Windows port works

Quickshell's core (QML engine, reloading, IPC, the `Quickshell` module) is portable C++ and builds for Windows
with small changes. Everything that talks to the desktop lives in `src/windows/` and replaces the Wayland and
Hyprland parts: the same QML types (`PanelWindow`, `WlrLayershell`, `Hyprland`, `ToplevelManager`, the `Services.*`
modules) are implemented on top of Win32, COM and WinRT, so a config written for Hyprland mostly runs as it is.

## Files and folders

| | |
|---|---|
| configs | `%LOCALAPPDATA%\quickshell\<name>\shell.qml`, run with `qs -c <name>` (or `qs -p <file>` from anywhere) |
| logs | `%LOCALAPPDATA%\quickshell\run\by-id\<instance>\log.log`, or `qs log` |
| crash dumps | `%LOCALAPPDATA%\quickshell\cache\crashes\<instance>\*.dmp` (minidumps, the shell restarts itself) |
| IPC | named pipes instead of Unix sockets; `qs ipc`, `qs list` and `qs kill` work the same |
| fonts | a `fonts` folder next to `qs.exe` is loaded at startup for that process only, skipping any family Windows already has installed (two versions of one family would mix their glyphs) |

`qs.exe` is a console program and `qsw.exe` is the same binary built for the Windows subsystem, so it doesn't open a
console window. Hot reload is on, like on Linux: saving a file in the config reloads it.

## Panels

`PanelWindow` becomes a `WinPanelWindow`:

- **Anchors and `exclusiveZone`** register an AppBar with the shell (`SHAppBarMessage`), so maximized windows
  leave the space free, the same way they do for the taskbar.
- **`WlrLayershell.layer`**: `Background` puts the window in the desktop, behind the icons; `Bottom` sits under
  normal windows; `Top` stays above them but steps aside while an app is fullscreen; `Overlay` stays above
  everything.
- **`keyboardFocus`**: `OnDemand` and `Exclusive` panels take the keyboard when they appear; `None` panels never
  take focus.
- **`mask`** works through a low-level mouse hook, so the transparent parts of a panel let clicks through.
- `HyprlandFocusGrab` closes on a click outside the panel or when another app comes to the front.

### Blur behind panels

Windows draws blur through a backdrop window under each panel (the Windows 11 host backdrop, or the older
`SetWindowCompositionAttribute` blur on Windows 10). Rules come from `defaults\windows\layerrules.json` inside the config folder:

```json
{
  "enable": true,
  "rules": [
    {"namespace": "quickshell:bar", "blur": true, "ignoreAlpha": 0.6},
    {"namespace": "quickshell:.*popup.*", "blur": true}
  ]
}
```

`namespace` is a regular expression matched against the whole `WlrLayershell.namespace`, later rules override
earlier ones, and `ignoreAlpha` limits the blur to `Rectangle` fills more opaque than that value (Windows can't
read the pixels, so text and images don't get blur of their own). Blur is off while Windows' transparency effects
are off, or high contrast or energy saver are on. `QS_WINDOWS_BLUR=0` turns it off for a run.

### Panel animations

A rule can also animate a panel when it appears and hides, like Hyprland's `layerrule = animation`:
`"animation": "slide"` (from the nearest edge), `"slide top"`/`"bottom"`/`"left"`/`"right"`, `"popin 80%"`, `"fade"`
or `"noAnim"`. Panels without one just appear. `BackdropBlur.panelAnimations` turns every animation off, and
`BackdropBlur.panelAnimationStyle: "expressive"` swaps the standard curves for springs, a content cascade and shader
effects that are only loaded while in use.

## Global shortcuts

Windows has no compositor to bind keys for you, so Quickshell does it, with `RegisterHotKey` and a low-level
keyboard hook (that's how the Windows key on its own can be a shortcut). The binds live in
`defaults\windows\keybinds.json` inside the config folder and reload on save:

```json
{
  "binds": [
    {"keys": "Super", "action": "global", "name": "launcherToggle", "onRelease": true},
    {"keys": "Super+Return", "action": "exec", "command": "wt.exe"},
    {"keys": "Super+Q", "action": "dispatch", "dispatch": "killactive"},
    {"keys": "Super+1", "action": "dispatch", "dispatch": "workspace 1"},
    {"keys": "Ctrl+Super+B", "action": "ipc", "target": "bar", "function": "toggle"}
  ]
}
```

- `global` fires the `GlobalShortcut` with that `name` in your config.
- `dispatch` runs a Hyprland dispatcher (below).
- `exec` starts a program. `%SHELL%` in the command is replaced by the config folder.
- `ipc` calls an `IpcHandler` function, with optional `args`.
- `key` sends another key (`"key": "VolumeUp"`).
- `reload` reloads the config.
- `native` leaves the keys to Windows.

Modifiers are `Ctrl`, `Alt`, `Shift` and `Super`; keys are letters, digits, `F1`-`F24` and names like `Return`,
`Space`, `Tab`, `Escape`, `Slash`, `Page_Down`, `Print` or `VolumeUp` (case doesn't matter).
Only the config's own `shell.qml` loads binds.

To let people override your binds without touching the config, name a user file in the defaults file with
`"userFile"`. A relative path starts at `%LOCALAPPDATA%`; when that file exists it wins over the defaults and is
reloaded on save. ii-windows uses `"userFile": "illogical-impulse/keybinds.json"`. `layerrules.json` takes the
same key. Without it, only the config's own file is read.

A user keybinds file can also list only its changes, so later changes to the defaults still reach it:

```json
{
  "extends": "defaults",
  "binds": [
    {"base": "global:quickshell:oskToggle", "disabled": true},
    {"base": "dispatch:killactive", "keys": "Super+C"},
    {"keys": "Super+E", "action": "exec", "command": "explorer.exe"}
  ]
}
```

`base` names a default bind by its action and argument (`global:<appid>:<name>`, `dispatch:<dispatcher>`,
`exec:<command>`, `ipc:<target>:<function>`, `key:<key>`), with `@<keys>` added when several defaults share it.
`keys` moves it, `disabled` gives its keys back to Windows, and binds without `base` are added.

## Why the modules have Linux names

The QML modules keep the names Quickshell uses on Linux (`Quickshell.Hyprland`, `Quickshell.Wayland`,
`Services.Pipewire`, `Services.Mpris`, `Services.UPower` and the others) on purpose: they're the same APIs, so a config
and Quickshell's own documentation work on both systems without changes. On Windows they mean:

| In QML | On Windows |
|---|---|
| `Hyprland` workspaces | virtual desktops |
| `Hyprland` monitors, `Quickshell.screens` | displays |
| `ToplevelManager` (`Quickshell.Wayland`) | the top-level app windows, the ones the taskbar shows |
| `PanelWindow`, `WlrLayershell` | topmost panels; with an exclusive zone, app bars that reserve screen space |
| `ScreencopyView`, `WindowThumbnail` | Windows.Graphics.Capture, and the window manager's own thumbnails |
| `Services.Pipewire` | Core Audio |
| `Services.Mpris` | the media controls in Windows' media flyout |
| `Services.UPower` | the battery and power plans |
| `Services.Notifications` | the shell's own notifications, plus a mirror of Windows' toasts |

What exists only on Windows is in `Quickshell.Windows`, under Windows names (see the end of [Services](#services)).

## Hyprland compatibility

`Quickshell.Hyprland` keeps its API, backed by Windows:

- **Workspaces** are virtual desktops. Switching and moving windows between them uses
  [VirtualDesktopAccessor](https://github.com/Ciantic/VirtualDesktopAccessor) when its DLL is next to `qs.exe`
  (the package ships the Windows 11 build, and the Windows 10 one in `win10\`). Without it, Quickshell falls back
  to the registry and keystrokes. Like on Hyprland, `Hyprland.workspaces` holds the current workspace and the ones
  with windows; desktop names show up as workspace names.
- **Monitors** come from the display configuration, and toplevels from a window tracker
  (`SetWinEventHook`), which also feeds `ToplevelManager` in `Quickshell.Wayland`.
- **`Hyprland.dispatch`** understands `workspace`, `movetoworkspace`, `movetoworkspacesilent`,
  `movecurrentworkspacetomonitor`, `focusmonitor`, `focuswindow`, `movefocus`, `movewindow`, `swapwindow`,
  `swapnext`, `cyclenext`, `killactive`, `closewindow`, `fullscreen`, `fullscreenstate`, `togglefloating`,
  `setfloating`, `settiled`, `pin`, `centerwindow`, `resizeactive`, `movewindowpixel`, `togglesplit`, `pseudo`,
  `layoutmsg`, `togglegroup`, `togglespecialworkspace`, `submap`, `exec` and `global`.
- **Tiling** like Hyprland's dwindle, master and scrolling layouts is there but optional, per desktop and monitor
  (`Quickshell.Windows` `Tiling`), and so is moving and resizing windows with Super + drag (`SuperDrag`). The
  scrolling layout keeps windows in columns on a strip that scrolls sideways (`layoutmsg` takes Hyprland's
  `colresize`, `fit`, `movewindowto` and friends). Windows can't clip other apps' windows, so columns out of view
  are moved just off screen, and they only peek in on screen edges with no other monitor next to them.
  `Tiling.scrollingPresets` sets the widths that `colresize +conf` and `-conf` step through, and Super with the
  sideways wheel (or Shift+Super with the normal one) moves to the next column on the monitor under the cursor.

For the active window, prefer `ToplevelManager.activeToplevel` from `Quickshell.Wayland`.

## Services

| Module | Backed by |
|---|---|
| `Services.Mpris` | GlobalSystemMediaTransportControls: any app in Windows' media flyout, with cover art |
| `Services.Pipewire` | Core Audio: endpoints, default device, volume, per-app sessions |
| `Services.Notifications` | a native server for the shell's own notifications, plus a mirror of Windows' toasts through UserNotificationListener |
| `Services.SystemTray` | the tray icons, read from Explorer's notification area |
| `Services.UPower` | battery status and power plans as power profiles |
| `Quickshell.Bluetooth` | WinRT device watcher, pairing and connecting |
| `Services.Pam`, `Services.Polkit`, `WlSessionLock` | inert stand-ins: Windows has its own lock screen and UAC |
| `DesktopEntries`, `Quickshell.iconPath` | the Start menu's apps folder and Shell icons |

`Quickshell.Windows` adds Windows-only singletons: `Network` (Wi-Fi included), `Brightness` (DDC/CI and WMI),
`Clipboard` (history), `Screenshot` and `Ocr`, `ScreenRecorder`, `Wallpaper`, `Displays`, `Session`
(lock, sign out, restart...), `Taskbar`, `Autostart`, `Credentials`, `Keyboard`, `Input`, `IdlePower`,
`SystemStats`, `SystemMonitor`, `WindowManager`, `Tiling`, `SuperDrag`, `CalendarEvents` (iCalendar feeds),
`AppActions` (run as administrator, open the file location or uninstall page), `WindowFrame` (hides a window's
native title bar so a QML window can draw its own) and a few more. They're what ii-windows
uses where Linux would call a command-line tool. `Autostart` can also make the shell start when you sign in, as a
scheduled task or a Run key entry, with the config that turned it on.

## Notes for contributors

- C++/WinRT runs on its own multithreaded apartment thread. Qt initializes COM as single-threaded on the GUI thread,
  and C++/WinRT doesn't allow blocking calls there, so every WinRT service owns a worker thread and posts its
  results back to Qt.
- Qt Quick uses Direct3D 11 on Windows; translucent frameless windows work as they are.
- With more than one window on screen, Qt advances animations from a timer sized by the primary screen's refresh
  rate, so a 60 Hz primary made everything step at 60 Hz on a faster monitor. Quickshell installs its own scene
  graph context (`qswindows`) that ticks at the fastest screen's rate (up to 250 Hz) with wall-clock timing, and
  slows to about 30 steps per second while animations run only in hidden windows. `QS_WINDOWS_ANIMATION_HZ` caps
  the rate, `0` turns it off, and setting `QT_QUICK_BACKEND` also leaves Qt's own timing in place.
- Windows that stay hidden for 10 seconds free their graphics resources (`QS_RELEASE_HIDDEN_MS`, `-1` turns it off).
- Unpackaged, the notification listener is allowed by default, but `NotificationChanged` throws, so the mirror polls
  every 2 seconds.

## Building

The release is cross-compiled from Linux:

- **Compiler and linker:** clang-cl and lld-link.
- **MSVC runtime and Windows SDK:** headers and libraries from [xwin](https://github.com/Jake-Shadle/xwin).
- **Qt 6.11.2:** the `msvc2022_64` build from aqtinstall, plus the Linux `gcc_64` build for Qt's host tools.

`windows/toolchain.sh` puts all of it in `.toolchain/`. Set `QSW_TOOLCHAIN` to keep it somewhere else.
`windows/fix-xwin-case.py` adds the header spellings C++/WinRT expects on a case-sensitive file system.
CMake links with lld-link directly (`windows/cmake/clang-cl-rules.cmake`), so neither rc.exe nor mt.exe is needed.

```sh
windows/toolchain.sh
windows/build.sh       # extra CMake options can go after it
windows/package.sh
```

The package bundles the Visual C++ runtime DLLs so it starts on a PC without the redistributable. They aren't
downloaded for you: extract `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`, `msvcp140_1.dll`,
`msvcp140_2.dll` and `msvcp140_atomic_wait.dll` from Microsoft's `VC_redist.x64.exe` (or copy them from
`System32` on a PC that has it) into `.toolchain/vcredist/`. They need to be at least as new as the CRT headers
xwin downloaded (`.toolchain/xwin/crt/include/crtversion.h`).

Building natively on Windows with Visual Studio should work through the normal CMake flow (see
[BUILD.md](BUILD.md)), but it isn't what the releases use and hasn't been tried.
