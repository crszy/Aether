# Wallpaper Engine helper for Aether (no Explorer). "open": load each monitor's chosen wallpaper from WE's config.json
# (WE does not do it itself without Explorer, so the desktop stays black). "pause" / "play": game mode.
# "stop": unload every wallpaper (you picked a normal one in Aether) so WE holds no memory or GPU.
param([string]$Action = "open")
# "desktop-show" / "desktop-hide": AetherWM's stand-in Progman (no Explorer) is black when WE isn't drawing and sits
# over Aether's wallpaper - hide it then, show it again for WE. A real Explorer Progman is never touched.
if ($Action -like "desktop-*") {
    Add-Type -Namespace AetherWe -Name U -MemberDefinition '[DllImport("user32.dll")] public static extern System.IntPtr FindWindow(string c, string t); [DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int n); [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(System.IntPtr h, out int p);'
    $h = [AetherWe.U]::FindWindow("Progman", "Program Manager")
    if ($h -eq [IntPtr]::Zero) { exit 0 }
    $owner = 0; [void][AetherWe.U]::GetWindowThreadProcessId($h, [ref]$owner)
    if ((Get-Process -Id $owner -ErrorAction SilentlyContinue).Name -eq "explorer") { exit 0 }
    [void][AetherWe.U]::ShowWindow($h, $(if ($Action -eq "desktop-show") { 8 } else { 0 }))   # SW_SHOWNA / SW_HIDE
    exit 0
}
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
