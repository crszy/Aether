# Puts together what Aether's installer installs, in dist\Aether, from the working install (CaelestiaShell,
# kept current by the deploy) plus the extras. Then make-installer.ps1 packs it into AetherSetup.exe.
#   powershell -File stage-aether.ps1
$ErrorActionPreference = 'Stop'
$root  = 'C:\Users\Administrator\Desktop\CaelestiaWin'
$live  = "$root\CaelestiaShell"
$dist  = "$root\dist\Aether"
$here  = $PSScriptRoot

# empty it rather than delete it (a shell or Explorer window sitting in the folder would block deleting it)
if (Test-Path $dist) { Get-ChildItem $dist -Force | Remove-Item -Recurse -Force }
New-Item -ItemType Directory -Force $dist | Out-Null

# 1. the program (no machine-specific Python venv, no logs, no stray folders)
robocopy $live $dist /E /NFL /NDL /NJH /NJS /NP /XD "$live\cli\venv" "$live\C:UsersAdministrator" /XF deploy.log *.pdb *.ilk *.before-* qs-hooktest.exe qs-jobtest.exe | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
# a folder left by a mistyped path once (it holds a copy of a Noctalia plugin list) - never shipped
Get-ChildItem -LiteralPath $dist -Directory | Where-Object { $_.Name -like 'C*Users*' } | Remove-Item -Recurse -Force

# 2. the Visual C++ runtime next to qs.exe (friends' PCs may not have it; app-local copies are allowed)
$crt = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT' -Directory |
       Sort-Object FullName -Descending | Select-Object -First 1
if (-not $crt) { throw 'Visual C++ runtime folder not found' }
Copy-Item "$($crt.FullName)\*.dll" "$dist\qs\" -Force

# 3. the settings background video (the one behind Aether settings)
New-Item -ItemType Directory -Force "$dist\media" | Out-Null
$video = "$env:USERPROFILE\.config\caelestia\settings-background.mp4"
if (Test-Path $video) { Copy-Item $video "$dist\media\aether-background.mp4" }

# 4. AetherWM (grown from a komorebi fork): SOURCE only - the Komorebi License allows sharing source, not builds.
#    Built on the friend's PC by build-aether-tiling.ps1 after they agree to the license.
$tiling = "$dist\extras\aether-tiling"
robocopy "$root\aether-tiling" $tiling /E /NFL /NDL /NJH /NJS /NP /XD target* media .git "$root\aether-tiling\fork-patches\before-aetherwm" /XF demo.mp4 *.before-* AGENTS.md   # every Rust build folder (target, target-gnu2...) and the demo media stay out | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy (tiling) failed ($LASTEXITCODE)" }
Copy-Item "$here\build-aether-tiling.ps1" "$tiling\"
Copy-Item "$here\starter-hyprland.conf" "$tiling\"
# GNU assembler (5 MB, GPL - source links in its README): lets the build run without Visual Studio
Copy-Item "$here\gnu-as" "$tiling\gnu-as" -Recurse -Force

# 5. source of the compiled parts (GPL / LGPL: the QML is already source; these are the C++ pieces)
$srcOut = "$dist\source"
robocopy "$root\quickshell-windows" "$srcOut\quickshell-windows" /E /NFL /NDL /NJH /NJS /NP /XD "$root\quickshell-windows\build" "$root\quickshell-windows\.git" /XF *.pdb *.obj | Out-Null
robocopy "$root\dotfiles\caelestia-shell-win\plugin" "$srcOut\caelestia-plugin" /E /NFL /NDL /NJH /NJS /NP | Out-Null
robocopy "$root\dotfiles\caelestia-launcher" "$srcOut\caelestia-launcher" *.cpp *.bat /NFL /NDL /NJH /NJS /NP | Out-Null
Copy-Item "$here\AetherSetup.cs" "$srcOut\" -ErrorAction SilentlyContinue

# 5b. both source trees ship as one zip each (thousands of small files make installing slow - Windows Defender
#     scans every file); build-aether-tiling.ps1 unzips its source when someone opts in
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Pack($dir, $zip) {
    if (Test-Path $zip) { Remove-Item $zip -Force }
    [System.IO.Compression.ZipFile]::CreateFromDirectory($dir, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)
    Remove-Item $dir -Recurse -Force
}
$tilingSrc = "$dist\extras\aether-tiling-src"
Move-Item $tiling $tilingSrc
New-Item -ItemType Directory -Force $tiling | Out-Null
foreach ($f in 'LICENSE.md', 'README.md', 'build-aether-tiling.ps1', 'starter-hyprland.conf', 'HYPRLAND-PLAN.md') { Move-Item "$tilingSrc\$f" "$tiling\" -ErrorAction SilentlyContinue }
Pack $tilingSrc "$tiling\source.zip"
Pack $srcOut "$dist\source.zip"

# 6. the setup (run by AetherSetup.exe from its temp copy): AetherSetup.qml goes into the config folder so it is
#    built from Caelestia's own parts; its media and finish script stay in setup-ui
New-Item -ItemType Directory -Force "$dist\setup-ui" | Out-Null
Copy-Item "$here\setup-ui\media" "$dist\setup-ui\media" -Recurse -Force
Copy-Item "$here\setup-ui\finish.ps1" "$dist\setup-ui\"
Copy-Item "$here\setup-ui\AetherSetup.qml" "$dist\config\AetherSetup.qml"

# 7. notice
Copy-Item "$here\NOTICE.txt" "$dist\NOTICE.txt"

$size = (Get-ChildItem $dist -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
"staged $dist  ({0:N0} MB)" -f $size

# 8. portable build: the same folder as a zip (unzip anywhere, run Caelestia.exe - no installer, no registry)
$portable = "$root\dist\Aether-portable.zip"
if (Test-Path $portable) { Remove-Item $portable -Force }
[System.IO.Compression.ZipFile]::CreateFromDirectory($dist, $portable, [System.IO.Compression.CompressionLevel]::Optimal, $true)
"portable $portable  ({0:N0} MB)" -f ((Get-Item $portable).Length / 1MB)
