# aetherwm - control Aether's window manager from any terminal.
#
#   aetherwm start [--niri | --hyprland | --infinitedesk]   start it (and switch config first if a mode is given)
#   aetherwm stop  [--niri | --hyprland | --infinitedesk]   stop it (a mode is remembered for the next start)
#   aetherwm restart [--niri | --hyprland | --infinitedesk]
#   aetherwm mode [niri | hyprland | infinitedesk]          show or set which config it runs
#   aetherwm reload                                         re-read the config (after you edit it)
#   aetherwm status
#
# Aether starts the window manager itself; this asks Aether (Settings > Windows > tiling) to start or stop it, so
# it stays started after a restart. Configs: hyprland = ~\.config\hypr\hyprland.conf,
# niri = ~\.config\niri\config.kdl, infinitedesk = ~\.config\hypr\infinitedesk.conf.
param([Parameter(Position = 0)][string]$Command = 'status', [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Rest)
$ErrorActionPreference = 'Stop'

$modes = 'hyprland', 'niri', 'infinitedesk'
$bin = Join-Path $env:LOCALAPPDATA 'AetherShell\aetherwm\bin'
$binSetting = ($bin -replace '\\', '/')
$modeFile = Join-Path $env:USERPROFILE '.config\aetherwm\mode'
$shellQml = (Join-Path $env:USERPROFILE '.config\quickshell\aether\shell.qml') -replace '\\', '/'
# qs.exe is only needed by start/stop: found on first use, from the known install places (a recursive search of
# AetherShell made every command, even `status`, take ~16 s)
$script:qs = $null
function Find-Qs {
    if ($script:qs) { return $script:qs }
    $running = Get-Process qs, qsw -ErrorAction SilentlyContinue | Where-Object Path | Select-Object -First 1 -ExpandProperty Path
    $script:qs = @($running,
        "$env:USERPROFILE\Desktop\CaelestiaWin\CaelestiaShell\qs\qs.exe",
        "$env:LOCALAPPDATA\Programs\Aether Shell\qs\qs.exe",
        "$env:LOCALAPPDATA\AetherShell\qs\qs.exe") | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if ($script:qs -and (Split-Path $script:qs -Leaf) -eq 'qsw.exe') { $script:qs = Join-Path (Split-Path $script:qs) 'qs.exe' }
    return $script:qs
}

function Get-Mode { if (Test-Path $modeFile) { (Get-Content $modeFile -Raw).Trim().ToLower() } else { 'hyprland' } }
function Set-Mode([string]$m) {
    New-Item -ItemType Directory -Force (Split-Path $modeFile) | Out-Null
    [IO.File]::WriteAllText($modeFile, $m)
    Write-Host "config: $m"
}
function Get-ModeArg {
    foreach ($a in $Rest) {
        $m = $a.TrimStart('-').ToLower()
        if ($modes -contains $m) { return $m }
        if ($a) { Write-Host "unknown option $a (use --niri, --hyprland or --infinitedesk)" -ForegroundColor Yellow; exit 1 }
    }
    return $null
}
function Running { [bool](Get-Process aetherwm -ErrorAction SilentlyContinue) }
function Shell-Set([string]$value) {   # '' = off (an empty argument, never the two characters "")
    $qs = Find-Qs
    if (-not $qs) { Write-Host 'Aether (qs.exe) not found - is Aether installed?' -ForegroundColor Red; exit 1 }
    # Windows PowerShell drops/mangles an empty native argument (the shell got a literal "" path): pass a raw
    # command line instead, where "" is a real empty argument
    $arg = if ($value) { "`"$value`"" } else { '""' }
    Start-Process $qs -ArgumentList "ipc -p `"$shellQml`" call windows set komorebi $arg" -NoNewWindow -Wait
}
function Start-Wm {
    if (Running) { Write-Host 'aetherwm is already running'; return }
    Shell-Set $binSetting
    for ($i = 0; $i -lt 20 -and -not (Running); $i++) { Start-Sleep -Milliseconds 500 }
    if (-not (Running)) {   # Aether only reacts to a change: switch it off and on again
        Shell-Set ''; Start-Sleep 1; Shell-Set $binSetting
        for ($i = 0; $i -lt 20 -and -not (Running); $i++) { Start-Sleep -Milliseconds 500 }
    }
    if (Running) { Write-Host "aetherwm started ($(Get-Mode))" -ForegroundColor Green } else { Write-Host 'aetherwm did not start - see %TEMP%\komorebi.log.*' -ForegroundColor Red; exit 1 }
}
function Stop-Wm {
    Shell-Set ''
    if (Test-Path "$bin\aetherwmc.exe") { & "$bin\aetherwmc.exe" stop 2>&1 | Out-Null }
    for ($i = 0; $i -lt 10 -and (Running); $i++) { Start-Sleep -Milliseconds 500 }
    Get-Process aetherwm -ErrorAction SilentlyContinue | Stop-Process -Force
    Write-Host 'aetherwm stopped'
}
function Send-Reload {
    $pipe = [IO.Directory]::GetFiles('\\.\pipe\') | Where-Object { $_ -like '*hypr*.socket.sock' } | Select-Object -First 1
    if (-not $pipe) { Write-Host 'aetherwm is not running'; exit 1 }
    $c = New-Object IO.Pipes.NamedPipeClientStream('.', $pipe.Substring(9), [IO.Pipes.PipeDirection]::InOut)
    $c.Connect(3000); $b = [Text.Encoding]::UTF8.GetBytes('reload'); $c.Write($b, 0, $b.Length); $c.Flush()
    $buf = New-Object byte[] 64; $n = $c.Read($buf, 0, 64); $c.Dispose()
    Write-Host "reload: $([Text.Encoding]::UTF8.GetString($buf, 0, $n))"
}

switch ($Command.ToLower()) {
    'start' { $m = Get-ModeArg; if ($m) { Set-Mode $m }; Start-Wm }
    'stop' { $m = Get-ModeArg; Stop-Wm; if ($m) { Set-Mode $m } }
    'restart' { $m = Get-ModeArg; Stop-Wm; if ($m) { Set-Mode $m }; Start-Wm }
    'mode' {
        $m = if ($Rest) { $Rest[0].TrimStart('-').ToLower() } else { $null }
        if (-not $m) { Write-Host (Get-Mode); break }
        if ($modes -notcontains $m) { Write-Host "modes: $($modes -join ', ')" -ForegroundColor Yellow; exit 1 }
        Set-Mode $m
        if (Running) { Write-Host 'restart to use it:  aetherwm restart' }
    }
    'reload' { Send-Reload }
    'status' {
        $p = Get-Process aetherwm -ErrorAction SilentlyContinue
        if ($p) { Write-Host "aetherwm running (pid $($p.Id)), config: $(Get-Mode)" } else { Write-Host "aetherwm stopped, config: $(Get-Mode)" }
    }
    { $_ -in 'help', '-h', '--help', '/?' } { Get-Content $PSCommandPath | Select-Object -Skip 1 -First 12 | ForEach-Object { $_ -replace '^# ?', '' } }
    default { Write-Host "unknown command $Command - try: aetherwm help" -ForegroundColor Yellow; exit 1 }
}
