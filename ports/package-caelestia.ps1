# Packs Caelestia-on-Windows into one standalone folder (its own Qt runtime), with Start/Stop scripts.
#   pwsh -File package-caelestia.ps1 [-Out <folder>]
param([string]$Out = 'C:\Users\Administrator\Desktop\CaelestiaWin\CaelestiaShell')
$ErrorActionPreference = 'Stop'
$root = 'C:\Users\Administrator\Desktop\CaelestiaWin'
$qsb  = "$root\quickshell-windows\build\native"
$cae  = "$root\dotfiles\caelestia-shell-win"
$m3   = "$root\dotfiles\m3shapes\build"
$qt   = 'C:\Qt\6.11.2\msvc2022_64'

# keep the user's own files (none live here; config/state go to their profile), replace the program files
foreach ($d in 'qs', 'caelestia', 'config') { if (Test-Path "$Out\$d") { Remove-Item "$Out\$d" -Recurse -Force } }
New-Item -ItemType Directory -Force "$Out\qs", "$Out\caelestia\lib", "$Out\caelestia\qml", "$Out\config" | Out-Null

# 1. Quickshell for Windows
Copy-Item "$qsb\qs.exe", "$qsb\qsw.exe" "$Out\qs\"
Copy-Item "$qsb\qml_modules" "$Out\qs\qml_modules" -Recurse
Copy-Item "$qsb\fonts" "$Out\qs\fonts" -Recurse
Copy-Item "$root\quickshell-windows\shims" "$Out\qs\shims" -Recurse
Copy-Item "$root\quickshell-windows\LICENSE", "$root\quickshell-windows\LICENSE-GPL" "$Out\qs\"

# 2. Caelestia plugin + M3Shapes
Copy-Item "$cae\build\lib\*.dll" "$Out\caelestia\lib\"
Copy-Item "$cae\build\qml\Caelestia" "$Out\caelestia\qml\Caelestia" -Recurse
Copy-Item "$m3\m3shapes.dll" "$Out\caelestia\lib\"
Copy-Item "$m3\qml\M3Shapes" "$Out\caelestia\qml\M3Shapes" -Recurse
Get-ChildItem "$Out\caelestia" -Recurse -Include *.pdb, *.lib, *.exp, *.ilk | Remove-Item -Force

# 3. the config (QML) - no build trees, no plugin sources
foreach ($d in 'assets', 'components', 'daemons', 'modules', 'services', 'utils', 'defaults') { Copy-Item "$cae\$d" "$Out\config\$d" -Recurse }
Copy-Item "$cae\shell.qml", "$cae\LICENSE" "$Out\config\"

# 4. Qt runtime next to qs.exe (windeployqt scans every QML file for imports)
$env:PATH = "$qt\bin;" + $env:PATH
& "$qt\bin\windeployqt6.exe" --release --no-translations --no-compiler-runtime --no-system-d3d-compiler `
    --qmldir "$Out\config" --qmldir "$Out\qs\shims" --qmldir "$Out\caelestia\qml" `
    --qml-deploy-dir "$Out\qs\qml" --dir "$Out\qs" "$Out\qs\qs.exe" *> "$Out\deploy.log"
if ($LASTEXITCODE) { throw "windeployqt failed, see $Out\deploy.log" }
# windeployqt only follows qs.exe's own imports: make sure the libraries the plugin DLLs link are there too
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
# a PATH entry with a stray quote makes vcvars64.bat fail silently, and then dumpbin finds nothing
$env:PATH = (($env:PATH -split ';') | ForEach-Object { $_.Trim('"') } | Where-Object { $_ }) -join ';'
$probe = cmd /c "`"$vc`" >nul 2>&1 && dumpbin /nologo /dependents `"$Out\qs\qs.exe`"" | Select-String 'Qt6Core'
if (-not $probe) { throw "dumpbin did not run (vcvars64 failed) - cannot check the DLLs" }
for ($pass = 0; $pass -lt 3; $pass++) {   # repeat: a newly copied Qt DLL can pull in another
  $have = @(Get-ChildItem "$Out\qs\*.dll", "$Out\caelestia\lib\*.dll" | ForEach-Object Name)
  $deps = Get-ChildItem "$Out\caelestia", "$Out\qs" -Recurse -Filter *.dll | ForEach-Object {
            cmd /c "`"$vc`" >nul 2>&1 && dumpbin /nologo /dependents `"$($_.FullName)`"" } |
          Select-String '^\s+(Qt6\S+\.dll)$' | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique
  $missing = $deps | Where-Object { $_ -notin $have }
  if (-not $missing) { break }
  foreach ($d in $missing) { Copy-Item "$qt\bin\$d" "$Out\qs\"; "  + $d" }
}
# Caelestia's Models module keeps its data in SQLite
New-Item -ItemType Directory -Force "$Out\qs\plugins\sqldrivers" | Out-Null
Copy-Item "$qt\plugins\sqldrivers\qsqlite.dll" "$Out\qs\plugins\sqldrivers\"

# 4b. the caelestia CLI (wallpaper -> Material You scheme, scheme switching): upstream Python + Windows patches,
#     run by qs\caelestia.exe through a private venv (Pillow + materialyoucolor) that is kept between repackages
py -I "$root\dotfiles\make-cli-win.py" | Out-Null
if (Test-Path "$Out\cli\src") { Remove-Item "$Out\cli\src" -Recurse -Force }
New-Item -ItemType Directory -Force "$Out\cli\src" | Out-Null
Copy-Item "$root\dotfiles\caelestia-cli-win\src\caelestia" "$Out\cli\src\" -Recurse
Copy-Item "$root\dotfiles\caelestia-cli\LICENSE" "$Out\cli\"
Copy-Item "$root\dotfiles\caelestia-launcher\caelestia.exe" "$Out\qs\"
Copy-Item "$root\dotfiles\caelestia-launcher\hyprctl.exe" "$Out\qs\"
# workspaces: moving other apps' windows between virtual desktops (Ciantic/VirtualDesktopAccessor 2024-12-16, MIT)
Copy-Item "$root\vda\VirtualDesktopAccessor.dll" "$Out\qs\"; New-Item -ItemType Directory -Force "$Out\qs\licenses" | Out-Null; Copy-Item "$root\vda\LICENSE.txt" "$Out\qs\licenses\VirtualDesktopAccessor-LICENSE.txt"
Copy-Item "$root\dotfiles\hyprlua\hyprlua.exe","$root\dotfiles\hyprlua\prelude.lua" "$Out\qs\"   # reads hyprland.lua (Lua 5.5.1)
# InfiniteDesk (the user's own infinite-canvas WM) = Hyprland layout "canvas"
robocopy "$root\infinitedesk" "$Out\infinitedesk" /E /NFL /NDL /NJH /NJS /NP | Out-Null
# Ryoku (github.com/Ryoku-dev/ryoku, GPL-3.0): its desktop (ryoku-port.js) + its Ryoku.Blobs plugin built for Windows
# (ryoku-plugin-build.bat). The plugin's backing DLL sits next to qsw.exe (Windows finds a plugin's DLLs there).
if (Test-Path "$root\dotfiles\ryoku-win\shell\shell.qml") {
  robocopy "$root\dotfiles\ryoku-win" "$Out\ryoku" /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
  if (Test-Path "$root\dotfiles\ryoku-plugin-build\qml\Ryoku\Blobs") {
    robocopy "$root\dotfiles\ryoku-plugin-build\qml\Ryoku\Blobs" "$Out\ryoku\Ryoku\Blobs" /E /NFL /NDL /NJH /NJS /NP | Out-Null
    Copy-Item "$root\dotfiles\ryoku-plugin-build\qml\Ryoku\Blobs\ryoku-blobs.dll" "$Out\qs\"
  }
  # Ryogami's wallpaper picker plugin (ryogami-picker-build.bat): the module, its backing DLL next to qsw.exe
  if (Test-Path "$root\dotfiles\ryogami-picker-build\qml\Ryoku\Ryogami") {
    robocopy "$root\dotfiles\ryogami-picker-build\qml\Ryoku\Ryogami" "$Out\ryoku\Ryoku\Ryogami" /E /NFL /NDL /NJH /NJS /NP /XF *.lib *.exp *.pdb | Out-Null
    Copy-Item "$root\dotfiles\ryogami-picker-build\qml\Ryoku\Ryogami\ryogami-picker.dll" "$Out\qs\"
  }
  # its Go programs built for Windows (ryoku-go-port.js + ryoku-go-build.sh): the daemon, Hub backend, Hyprland
  # provider, Ryogami, the store - RyokuHost puts ryoku\bin first on Ryoku's PATH
  if (Test-Path "$root\dotfiles\ryoku-go-build\ryoku-shell.exe") {
    New-Item -ItemType Directory -Force "$Out\ryoku\bin" | Out-Null
    Copy-Item "$root\dotfiles\ryoku-go-build\*.exe" "$Out\ryoku\bin\"
  }
  Copy-Item "$root\ref\ryoku\LICENSE" "$Out\qs\licenses\Ryoku-LICENSE.txt" -ErrorAction SilentlyContinue
}
if (-not (Test-Path "$Out\cli\venv\Scripts\pythonw.exe")) {
  py -3.12 -m venv "$Out\cli\venv"
  & "$Out\cli\venv\Scripts\python.exe" -m pip install -q --disable-pip-version-check --only-binary=:all: pillow materialyoucolor
}

# 5. one program: Caelestia.exe in the root starts it / opens Settings / --stop / --restart
Copy-Item "$root\dotfiles\caelestia-launcher\out\Caelestia.exe" "$Out\Caelestia.exe"
foreach ($old in 'Start Caelestia.bat', 'Stop Caelestia.bat') { if (Test-Path "$Out\$old") { Remove-Item "$Out\$old" -Force } }
@'
Caelestia for Windows
=====================
Run Caelestia.exe. Run it again while Caelestia is open to bring up Settings.
  Caelestia.exe --settings   open Settings        Caelestia.exe --stop     quit
  Caelestia.exe --restart    restart
Everything else is in Settings (Super+Comma, or run Caelestia.exe again):
  Windows        hide the Windows taskbar, start with Windows, lock screen, wallpaper folder,
                 every config file with an Open button, reload / restart / quit
  All settings   every option in shell.json (bar, launcher, dashboard, lock, appearance...), searchable
  Panels > Taskbar, Wallpaper & style, Network, Audio, Apps, ...  Caelestia's own pages

Keys (Settings > Windows > Keyboard shortcuts opens the file to change them):
  Super (tap)     launcher            Super+A        dashboard
  Super+N         sidebar             Super+U        utilities
  Super+Comma     settings            Super+Escape   power menu
  Super+Shift+R   reload

Lock screen: Caelestia's lock, unlocked with your Windows password (Settings > Windows can switch to the
Windows lock screen instead). Caelestia's lock is drawn over the desktop - Ctrl+Alt+Del still reaches Windows.
Colours: pick a wallpaper with the "dynamic" scheme and the whole shell re-themes from it (needs Python 3.12).
Temperatures: GPU from Windows; CPU only while Aether's LibreHardwareMonitor sidecar runs.

Built from: caelestia-dots/shell + cli (GPL-3.0), nunreasonable/quickshell-windows (LGPL-3.0),
soramanew/m3shapes (Apache-2.0), Qt 6.11.2 (LGPL-3.0).
'@ | Set-Content "$Out\README.txt" -Encoding utf8

$size = (Get-ChildItem $Out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
"packed into $Out  ({0:N0} MB)" -f $size
