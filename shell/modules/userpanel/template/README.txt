Your own Aether parts - no dev build, no compiling. Save a file and it is live.

  commands.json        your launcher commands. Type ">" then the name in the launcher (">profile").
                       Each command does one or more of:
                         "panel": "profile"              open panels/profile.qml
                         "exec":  ["wt.exe", "-d", "C:\\"] run a program
                         "set":   { "autoGameMode": false } change an Aether setting (windows.json keys)
                         "ipc":   ["lock", "lock"]         call an Aether IPC function (qs ipc show lists them)
                         "url":   "https://..."            open a link
                       plus "name", "description" and "icon" (a Material Symbols name, fonts.google.com/icons).

  panels/<name>.qml    a panel that opens over the desktop. Esc or a click outside closes it.
                       Open it from a command, from a widget (UserExt.openPanel("name")) or a script:
                         qs ipc call user panel <name>

  widgets/<Name>.qml   a bar widget. Add "user:<Name>" to a bar:
                         Caelestia's bar  -> bar.json entries: { "id": "user:Profile", "enabled": true }
                         end-4, Noctalia, DMS, Zenith bars -> windows.json "barPieces": ["user:Profile"]
                       It gets `vertical` (true in a side bar) and `screen` if it declares them.

  lock/<Name>.qml      a whole lock screen:            windows.json "mixLock": "user:<Name>"   (example: AetherLock)
                       or just the clock / media card: "lockClock" / "lockMedia": "user:<Name>" (example: BigClock)
                       Typing goes to the password check by itself; the file shows pam.buffer, pam.lockMessage.

  osd/<Name>.qml       the volume / brightness pop-up: windows.json "mixOsd": "user:<Name>"     (example: AetherPill)
                       It gets osd.kind ("volume" | "mic" | "brightness") and osd.volume / .brightness (0..1),
                       and may set `position` ("bottom", "top-right", "center" ...) and `margin`.

  dashboard/<Name>.qml an extra tab in Caelestia's dashboard, titled <Name>.               (example: _Links)

  desktop/<Name>.qml   a widget on your desktop, behind your apps. Where it sits: desktop.json (example: _Clock)
                         { "Clock": { "screen": "main" | "all" | <monitor>, "corner": "top-right", "x": 60, "y": 60 } }

  mods/<Name>.qml      code that runs inside Aether, like Windhawk mods do for Windows apps. It can react to
                       anything (windows opening, workspaces, music, time, battery, volume) and change anything
                       (settings, skin, wallpaper, colours, windows, panels, programs). It gets `aether` helpers:
                         aether.set("mixBar", "dms")  aether.skin("TokyoRGB")  aether.wallpaper("C:/x.jpg")
                         aether.scheme("dynamic")      aether.dispatch("workspace 2")  aether.panel("profile")
                         aether.run("profile")         aether.exec(["wt.exe"])  aether.notify("Hi", "text", "icon")
                       Examples: _NightTokyo (Tokyo skin at night), _WorkspaceWallpapers (a wallpaper per workspace).
                       On / off without renaming:  qs ipc call user mod <Name> on|off

  skins/<Name>/        a whole look for Aether's own windows - pick it: windows.json "aetherSkin": "<Name>"
                       (or a mod / launcher command: { "set": { "aetherSkin": "TokyoRGB" } })
                         style.json           recolour everything Aether draws: any m3* colour, "radius"
                         settings-back.qml    drawn behind Aether Settings (gets `surface`, `active`)
                         settings-front.qml   drawn on top of it (borders, glows; clicks pass through)
                       Example: TokyoRGB - neon night city with a turning RGB border.

  style.json           your colour / corner overrides for all of Aether, without a skin (same keys as above).

  terminals.json       your terminals in your colours: Alacritty, Windows Terminal, and any other terminal or app
                       through a template file. Set the text colour (foreground), background, cursor, selection and
                       the 16 terminal colours - "" follows your wallpaper, "#ff88cc" or a scheme name ("primary").

  panels/AetherSession the session menu as a dot that grows into a row of buttons.
                       Make it THE session menu: windows.json "sessionMenu": "user:AetherSession"

  panels/AetherCoverflow   your wallpapers as a 3D coverflow. Make it THE wallpaper picker (">wallpaper"):
                       Aether Settings > Wallpaper > Wallpaper picker, or windows.json "wallpaperPicker": "user:AetherCoverflow"

  desktop/_AetherBars  music visualizer bars along the bottom of your desktop (rename to AetherBars.qml to switch on)

A file whose name starts with "_" is switched off - rename it to switch it on.
To see every file Aether found:  qs ipc call user files

Your files are plain Quickshell QML and can use what Aether's own parts use:
  import qs.services     Colours.palette.m3primary ..., Players.active (music), Audio, Time, WinSettings, UserExt
  import qs.components   StyledText, StyledRect, MaterialIcon, ...
  import qs.utils        Paths (Paths.home, Paths.config ...)
  Quickshell docs: https://quickshell.org/docs/

If a file has a mistake, the panel shows the error (file, line, message) and a widget shows a red "!" -
hover it to read the error. Nothing else breaks.
