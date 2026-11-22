# Aether as your sign-in shell, instead of Explorer (this user only).
#
#   aether-session.ps1            shows which shell you sign in to
#   aether-session.ps1 -Enable    sign in to Aether from the next sign-in (the old setting is saved first)
#   aether-session.ps1 -Disable   back to Explorer from the next sign-in (puts the saved setting back)
#
# With Aether as the shell, "Caelestia.exe --session" starts your startup apps (Task Manager's Startup list, minus the
# ones switched off there) and then Aether. Stuck without a shell? Ctrl+Alt+Del > Task Manager > Run new task >
# explorer   (and run this with -Disable).
param([switch]$Enable, [switch]$Disable)
$ErrorActionPreference = 'Stop'

$key = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon'
$backup = Join-Path $env:LOCALAPPDATA 'AetherShell\shell-backup.json'
$pkg = Split-Path (Split-Path $PSScriptRoot)   # ...\CaelestiaShell\config\assets -> ...\CaelestiaShell
if (-not (Test-Path "$pkg\Caelestia.exe")) { $pkg = 'C:\Users\Administrator\Desktop\CaelestiaWin\CaelestiaShell' }
$launcher = Join-Path $pkg 'Caelestia.exe'
$aether = "`"$launcher`" --session"

function Get-Shell { (Get-ItemProperty $key -Name Shell -ErrorAction SilentlyContinue).Shell }

if ($Enable) {
    if (-not (Test-Path $launcher)) { Write-Host "Caelestia.exe not found at $launcher" -ForegroundColor Red; exit 1 }
    $current = Get-Shell
    if ($current -eq $aether) { Write-Host 'Aether is already your sign-in shell.'; exit 0 }
    New-Item -ItemType Directory -Force (Split-Path $backup) | Out-Null
    @{ had = [bool]$current; shell = $current; saved = (Get-Date).ToString('s') } | ConvertTo-Json | Set-Content $backup -Encoding utf8
    Set-ItemProperty $key -Name Shell -Value $aether -Type String
    Write-Host "Done: you sign in to Aether from the next sign-in. Saved the old setting to $backup." -ForegroundColor Green
    Write-Host 'Back to Explorer any time: the "Back to Explorer" shortcut on the desktop, or this script with -Disable.'
    exit 0
}

if ($Disable) {
    if (Test-Path $backup) {
        $saved = Get-Content $backup -Raw | ConvertFrom-Json
        if ($saved.had) { Set-ItemProperty $key -Name Shell -Value $saved.shell -Type String }
        else { Remove-ItemProperty $key -Name Shell -ErrorAction SilentlyContinue }
        Remove-Item $backup
    } elseif ((Get-Shell) -eq $aether) {
        Remove-ItemProperty $key -Name Shell -ErrorAction SilentlyContinue
    }
    Write-Host 'Done: you sign in to Explorer again from the next sign-in.' -ForegroundColor Green
    if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }
    exit 0
}

$current = Get-Shell
if ($current -eq $aether) { Write-Host 'You sign in to: Aether (Caelestia.exe --session)' }
elseif ($current) { Write-Host "You sign in to: $current (your own setting)" }
else { Write-Host 'You sign in to: Explorer (Windows default)' }
