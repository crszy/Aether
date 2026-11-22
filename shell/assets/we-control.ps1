# Wallpaper Engine helper for Aether (no Explorer). "open": load each monitor's chosen wallpaper from WE's config.json
# (WE does not do it itself without Explorer, so the desktop stays black). "pause" / "play": game mode.
# "stop": unload every wallpaper (you picked a normal one in Aether) so WE holds no memory or GPU.
param([string]$Action = "open")
$dir = "C:\Program Files (x86)\Steam\steamapps\common\wallpaper_engine"
$p = Get-Process wallpaper64, wallpaper32 -ErrorAction SilentlyContinue | Select-Object -First 1
if ($p -and $p.Path) { $dir = Split-Path $p.Path }
$exe = Join-Path $dir "wallpaper64.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $dir "wallpaper32.exe" }
if (-not (Test-Path $exe)) { exit 1 }
if ($Action -ne "open") { & $exe -control $Action; exit 0 }
$cfg = Get-Content (Join-Path $dir "config.json") -Raw | ConvertFrom-Json
foreach ($user in $cfg.PSObject.Properties) {
    $sel = $user.Value.general.wallpaperconfig.selectedwallpapers
    if (-not $sel) { continue }
    foreach ($m in $sel.PSObject.Properties) {
        if ($m.Name -notmatch '^Monitor(\d+)$' -or -not $m.Value.file) { continue }
        & $exe -control openWallpaper -file $m.Value.file -monitor $Matches[1]
        Start-Sleep -Milliseconds 300
    }
    break
}
