# Aether lean profile: fewer things competing with you at sign-in and while you play, nothing removed.
#
#   lean-profile.ps1            check only: shows what would change and the state of the big switches (no changes)
#   lean-profile.ps1 -Apply     (as administrator) applies it, saving every old value first
#   lean-profile.ps1 -Restore   (as administrator) puts every saved value back
#
# What it does, and why (see the Windows research journal):
#   - Background services that do not need to be ready the moment you sign in are set to "Automatic (Delayed Start)":
#     Windows starts them after everything else, at the lowest CPU and disk priority. They all keep working.
#     (Delayed start instead of disabling keeps every feature - journal section 9.2, H4.)
#   - It does NOT touch: Memory Integrity / VBS (a security control: reported only, your choice), the drive
#     optimisation task (it protects SSDs - section 7.3), timer-resolution or TCP registry "tweaks" (ignored or
#     useless on current Windows - sections 5.1, 8.7), RAM "cleaners" (they throw away useful cache - section 3.2).
param([switch]$Apply, [switch]$Restore)
$ErrorActionPreference = 'Stop'

$backup = Join-Path $env:LOCALAPPDATA 'AetherShell\lean-backup.json'

# services that can start a little later without losing anything
$delayed = [ordered]@{
    'WSearch'   = 'Windows Search (indexing)'
    'DiagTrack' = 'Connected User Experiences and Telemetry'
    'DoSvc'     = 'Delivery Optimization (update downloads)'
    'Spooler'   = 'Print Spooler'
    'WpnService' = 'Windows Push Notifications (system)'
    'TabletInputService' = 'Touch Keyboard and Handwriting'
    'lfsvc'     = 'Geolocation'
    'SysMain'   = 'SysMain (prefetching into spare RAM)'
}

function Test-Admin {
    $id = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    $id.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-StartMode([string]$name) {
    $key = "HKLM:\SYSTEM\CurrentControlSet\Services\$name"
    if (-not (Test-Path $key)) { return $null }
    $p = Get-ItemProperty $key
    $start = $p.Start
    $delayedAuto = [bool]$p.DelayedAutostart
    switch ($start) {
        2 { if ($delayedAuto) { 'delayed' } else { 'auto' } }
        3 { 'manual' }
        4 { 'disabled' }
        default { "start=$start" }
    }
}

function Show-State {
    Write-Host ''
    Write-Host 'Services (only the ones set to start right away are changed):' -ForegroundColor Cyan
    foreach ($name in $delayed.Keys) {
        $mode = Get-StartMode $name
        if (-not $mode) { continue }
        $note = if ($mode -eq 'auto') { '-> would become delayed start' } else { '(left as it is)' }
        '  {0,-20} {1,-10} {2}  {3}' -f $name, $mode, $note, $delayed[$name] | Write-Host
    }

    Write-Host ''
    Write-Host 'Big switches (reported only - change them yourself if you decide to):' -ForegroundColor Cyan
    try {
        $dg = Get-CimInstance -ClassName Win32_DeviceGuard -Namespace root\Microsoft\Windows\DeviceGuard -ErrorAction Stop
        $vbs = switch ($dg.VirtualizationBasedSecurityStatus) { 0 { 'off' } 1 { 'configured, not running' } 2 { 'running' } default { 'unknown' } }
        $hvci = if ($dg.SecurityServicesRunning -contains 2) { 'on' } else { 'off' }
        Write-Host "  VBS: $vbs, Memory Integrity: $hvci"
        if ($hvci -eq 'on') {
            Write-Host '    Measured cost in games: about 4-8% in CPU-heavy games at 1080p, about 2% at 4K (Tom''s Hardware,'
            Write-Host '    ComputerBase). It protects the kernel from malicious drivers. Windows Security > Device security.'
        }
    } catch { Write-Host '  VBS / Memory Integrity: could not be read' }
    $hags = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\GraphicsDrivers' -ErrorAction SilentlyContinue).HwSchMode
    $hagsText = switch ($hags) { 2 { 'on' } 1 { 'off' } default { 'not set (driver default)' } }
    Write-Host "  Hardware-accelerated GPU scheduling: $hagsText (small effect either way on fast CPUs)"
    $defrag = Get-ScheduledTask -TaskPath '\Microsoft\Windows\Defrag\' -TaskName 'ScheduledDefrag' -ErrorAction SilentlyContinue
    if ($defrag) { Write-Host "  Drive optimisation task: $($defrag.State) (keep it: it re-TRIMs SSDs)" }
    Write-Host ''
}

if ($Restore) {
    if (-not (Test-Admin)) { Write-Host 'Run this as administrator.' -ForegroundColor Yellow; exit 1 }
    if (-not (Test-Path $backup)) { Write-Host "Nothing to restore ($backup not found)."; exit 0 }
    $saved = Get-Content $backup -Raw | ConvertFrom-Json
    foreach ($s in $saved.services) {
        $key = "HKLM:\SYSTEM\CurrentControlSet\Services\$($s.name)"
        if (-not (Test-Path $key)) { continue }
        Set-ItemProperty $key -Name Start -Value ([int]$s.start) -Type DWord
        if ($null -eq $s.delayed) { Remove-ItemProperty $key -Name DelayedAutostart -ErrorAction SilentlyContinue }
        else { Set-ItemProperty $key -Name DelayedAutostart -Value ([int]$s.delayed) -Type DWord }
        Write-Host "  restored $($s.name)"
    }
    Remove-Item $backup
    Write-Host 'Restored. Takes full effect after a restart.' -ForegroundColor Green
    exit 0
}

if (-not $Apply) {
    Show-State
    Write-Host 'Nothing was changed. Run with -Apply (as administrator) to apply, -Restore to undo.' -ForegroundColor DarkGray
    exit 0
}

if (-not (Test-Admin)) { Write-Host 'Run this as administrator.' -ForegroundColor Yellow; exit 1 }
if (Test-Path $backup) { Write-Host "Already applied (backup at $backup). Run -Restore first to apply again." -ForegroundColor Yellow; exit 1 }

$saved = @()
foreach ($name in $delayed.Keys) {
    if ((Get-StartMode $name) -ne 'auto') { continue }
    $key = "HKLM:\SYSTEM\CurrentControlSet\Services\$name"
    $p = Get-ItemProperty $key
    $saved += [pscustomobject]@{ name = $name; start = $p.Start; delayed = $p.DelayedAutostart }
}
New-Item -ItemType Directory -Force (Split-Path $backup) | Out-Null
@{ applied = (Get-Date).ToString('s'); services = $saved } | ConvertTo-Json -Depth 4 | Set-Content $backup -Encoding utf8
foreach ($s in $saved) {
    # sc.exe is the supported way to set delayed start (it also tells the Service Control Manager)
    & sc.exe config $s.name start= delayed-auto | Out-Null
    Write-Host "  $($s.name): delayed start"
}
Write-Host "Applied. Backup: $backup  (undo with -Restore). Takes full effect after a restart." -ForegroundColor Green
