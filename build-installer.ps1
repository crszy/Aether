# Builds Aether-Setup.exe — one self-contained, self-extracting installer.  (V1.5)
#
# V1 embedded every shipped file as its own RCDATA resource. That stopped scaling the moment Aether
# started shipping the icon theme: 28,000 resources is a .rc file rc.exe takes minutes to chew on.
#
# So this compiles installer.cpp into a stub and then APPENDS the payload to it, the way a
# self-extracting archive works. Compression is the Windows Compression API (Cabinet.dll, MSZIP) —
# the same API installer.cpp decompresses with, so there is no library to vendor. MSZIP was measured
# against the alternatives on the real payload: 10.1x at 25 MB/s, versus LZMS's 8.6x at 3.6 MB/s.
#
# Usage:  .\build-installer.ps1  [-NoWallpapers] [-SelfContainedSensors]
#
#   -NoWallpapers          leave out linux\wallpapers\cachyos (-26 MB; the wallpaper picker then
#                          only offers what it finds on the target machine)
#   -SelfContainedSensors  republish the LibreHardwareMonitor sidecar with the .NET runtime inside
#                          it (+30 MB). Without this the sensor readouts need the .NET 8 Desktop
#                          Runtime on the target PC - everything ELSE in Aether is native and needs
#                          nothing installed, so this is the one dependency worth deciding about.
param([switch]$NoWallpapers, [switch]$SelfContainedSensors)

$ErrorActionPreference = "Stop"
$root   = $PSScriptRoot
$inst   = "$root\installer"
$stub   = "$inst\Aether-Setup-stub.exe"
$outExe = "$root\Aether-Setup.exe"
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
# Any other edition (Build Tools, Professional, the Enterprise that GitHub's runners carry): ask vswhere where it is.
if (-not (Test-Path $vcvars)) {
  $vsw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path $vsw) {
    $vsi = & $vsw -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsi) { $vcvars = "$vsi\VC\Auxiliary\Build\vcvars64.bat" }
  }
}
if (-not (Test-Path $vcvars)) { Write-Host "Visual Studio 2022 with the C++ workload was not found." -ForegroundColor Red; exit 1 }

if (-not (Test-Path "$root\Aether.exe")) { & "$root\build.ps1" }
if (-not (Test-Path "$root\Aether.exe")) { Write-Host "Aether.exe did not build - not packing an installer." -ForegroundColor Red; exit 1 }

# ---- what ships --------------------------------------------------------------------------------
# Everything the shell resolves at runtime relative to its own exe. Miss one of these and the
# install looks broken on someone else's PC in a way that is hard to diagnose: no font, no icons,
# no wallpapers, no sensor readings.
# The sidecar is framework-dependent by default (1.9 MB, wants .NET 8 on the target machine).
# -SelfContainedSensors republishes it with the runtime baked in, and the payload picks that up
# instead - same relative path inside the install, so nothing on the shell's side changes.
$sensorDir = "sensors"
if ($SelfContainedSensors) {
  $scDir = "$root\sensors-sc"
  Write-Host "Publishing a self-contained sensor sidecar..." -ForegroundColor Cyan
  Remove-Item $scDir -Recurse -Force -EA SilentlyContinue
  Push-Location "$root\sidecar"
  $pub = dotnet publish -c Release -r win-x64 --self-contained true `
           -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o $scDir 2>&1
  Pop-Location
  if (-not (Test-Path "$scDir\AetherSensors.exe")) {
    Write-Host "sidecar publish failed:" -ForegroundColor Red; $pub | Select-Object -Last 15 | ForEach-Object { Write-Host $_ }; exit 1
  }
  # the Mono helpers the framework-dependent build ships alongside are not in the publish output
  foreach ($m in @("MonoPosixHelper.dll","libMonoPosixHelper.dll")) {
    if (Test-Path "$root\sensors\$m") { Copy-Item "$root\sensors\$m" "$scDir\$m" -Force }
  }
  $sensorDir = "sensors-sc"
}

$specs = @(
  @{ Dir = "";                        Filter = "Aether.exe" }
  @{ Dir = "assets";                  Recurse = $true }
  @{ Dir = "presets";                 Recurse = $true }
  @{ Dir = "";                        Filter = "README.md" }
  @{ Dir = "plugins";                 Recurse = $true }
  @{ Dir = "theme";                   Recurse = $true }   # the Catppuccin msstyles the settings applies
  @{ Dir = $sensorDir;                Recurse = $true }   # LibreHardwareMonitor sidecar
  @{ Dir = "linux\fonts";             Recurse = $true }   # Adwaita, the fallback when Google Sans is absent
  @{ Dir = "linux\icons";             Recurse = $true }   # Adwaita + breeze + breeze-dark
)
if (-not $NoWallpapers) {
  # only the folder WallScan actually reads; linux\wallpapers\Next is another 40 MB nothing loads
  $specs += @{ Dir = "linux\wallpapers\cachyos"; Recurse = $true }
}
# .rcc is a 26 MB Qt resource bundle of the same icons we read as loose SVGs; .pdb is debug symbols.
$skipExt = @(".rcc", ".pdb", ".old")

Write-Host "Collecting payload..." -ForegroundColor Cyan
$files = @()                       # @{ Rel = "linux\icons\..."; Full = "Z:\..." ; Len = 1234 }
foreach ($sp in $specs) {
  $base = if ($sp.Dir) { "$root\$($sp.Dir)" } else { $root }
  if (-not (Test-Path $base)) { Write-Host "  missing: $base" -ForegroundColor Red; exit 1 }
  $gci = @{ Path = $base; File = $true }
  if ($sp.Recurse) { $gci.Recurse = $true }
  if ($sp.Filter)  { $gci.Filter  = $sp.Filter }
  foreach ($f in Get-ChildItem @gci) {
    if ($skipExt -contains $f.Extension.ToLower()) { continue }
    $rel = $f.FullName.Substring($root.Length + 1)
    # sensors-sc is a build artefact name; inside the install it is just sensors\
    if ($rel.StartsWith("sensors-sc\")) { $rel = "sensors\" + $rel.Substring(11) }
    $files += @{ Rel = $rel; Full = $f.FullName; Len = $f.Length }
  }
}
if ($files.Count -eq 0) { Write-Host "no payload" -ForegroundColor Red; exit 1 }
$rawTotal = ($files | Measure-Object -Property Len -Sum).Sum
Write-Host ("  {0} files, {1:N1} MB" -f $files.Count, ($rawTotal / 1MB))

# ---- compile the stub --------------------------------------------------------------------------
Write-Host "Compiling setup stub..." -ForegroundColor Cyan
$cmd = "`"$vcvars`" >nul 2>&1 && cd /d `"$inst`" && " +
       "cl /nologo /EHsc /std:c++17 /O2 /MT /DUNICODE /D_UNICODE /DWIN32 " +
       "/Fe`"$stub`" installer.cpp /link /SUBSYSTEM:WINDOWS"
$out = cmd /c $cmd 2>&1
$errs = $out | Select-String -Pattern "error [A-Z]|LNK[0-9]|fatal" | Select-Object -First 20
if ($errs) { Write-Host "SETUP BUILD FAILED:" -ForegroundColor Red; $errs | ForEach-Object { Write-Host $_ }; exit 1 }
if (-not (Test-Path $stub)) { Write-Host "No stub. Full output:" -ForegroundColor Red; $out | ForEach-Object { Write-Host $_ }; exit 1 }

# ---- the packer --------------------------------------------------------------------------------
$cs = @'
using System;
using System.Runtime.InteropServices;
public static class Cab {
  [DllImport("Cabinet.dll", SetLastError=true)]
  public static extern bool CreateCompressor(uint alg, IntPtr alloc, out IntPtr h);
  [DllImport("Cabinet.dll", SetLastError=true)]
  public static extern bool Compress(IntPtr h, byte[] u, IntPtr us, byte[] c, IntPtr cs, out IntPtr got);
  [DllImport("Cabinet.dll", SetLastError=true)]
  public static extern bool CloseCompressor(IntPtr h);
}
'@
if (-not ("Cab" -as [type])) { Add-Type -TypeDefinition $cs }

$COMPRESS_ALGORITHM_MSZIP = 2
$hComp = [IntPtr]::Zero
if (-not [Cab]::CreateCompressor($COMPRESS_ALGORITHM_MSZIP, [IntPtr]::Zero, [ref]$hComp)) {
  Write-Host "CreateCompressor failed" -ForegroundColor Red; exit 1
}

# Chunks never straddle a file, so the installer only ever holds one chunk in memory and the
# progress bar can move per chunk. A file larger than the target simply gets a chunk of its own.
$CHUNK_TARGET = 8MB

Copy-Item $stub $outExe -Force
$fs = [IO.File]::Open($outExe, [IO.FileMode]::Open, [IO.FileAccess]::Write)
[void]$fs.Seek(0, [IO.SeekOrigin]::End)
$table = New-Object System.Collections.Generic.List[object]

$chunk    = New-Object System.IO.MemoryStream
$chunkW   = New-Object System.IO.BinaryWriter($chunk)
$chunkN   = 0
$compTotal = 0L

function Flush-Chunk {
  if ($script:chunkN -eq 0) { return }
  $script:chunkW.Flush()
  $body = $script:chunk.ToArray()
  # record count goes in front of the records
  $raw = New-Object byte[] ($body.Length + 4)
  [BitConverter]::GetBytes([uint32]$script:chunkN).CopyTo($raw, 0)
  $body.CopyTo($raw, 4)

  $need = [IntPtr]::Zero
  [void][Cab]::Compress($script:hComp, $raw, [IntPtr]$raw.Length, $null, [IntPtr]::Zero, [ref]$need)
  $cap = [int]$need; if ($cap -lt $raw.Length + 65536) { $cap = $raw.Length + 65536 }
  $cbuf = New-Object byte[] $cap
  $got = [IntPtr]::Zero
  if (-not [Cab]::Compress($script:hComp, $raw, [IntPtr]$raw.Length, $cbuf, [IntPtr]$cap, [ref]$got)) {
    throw "Compress failed (win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
  }
  $n = [int]$got
  $script:table.Add([pscustomobject]@{ Off = $script:fs.Position; Comp = $n; Raw = $raw.Length })
  $script:fs.Write($cbuf, 0, $n)
  $script:compTotal += $n

  $script:chunk  = New-Object System.IO.MemoryStream
  $script:chunkW = New-Object System.IO.BinaryWriter($script:chunk)
  $script:chunkN = 0
}

Write-Host "Packing..." -ForegroundColor Cyan
$done = 0
foreach ($f in $files) {
  $bytes = [IO.File]::ReadAllBytes($f.Full)
  $path  = [Text.Encoding]::UTF8.GetBytes($f.Rel)
  if ($path.Length -gt 65535) { throw "path too long: $($f.Rel)" }
  $chunkW.Write([uint16]$path.Length)
  $chunkW.Write($path)
  $chunkW.Write([uint32]$bytes.Length)
  $chunkW.Write($bytes)
  $chunkN++
  if ($chunk.Length -ge $CHUNK_TARGET) { Flush-Chunk }
  $done++
  if (($done % 2000) -eq 0) { Write-Host ("  {0}/{1}" -f $done, $files.Count) -ForegroundColor DarkGray }
}
Flush-Chunk
[void][Cab]::CloseCompressor($hComp)

# chunk table, then the 32-byte footer the installer reads backwards from EOF
$tableOff = $fs.Position
$bw = New-Object System.IO.BinaryWriter($fs)
foreach ($t in $table) { $bw.Write([uint64]$t.Off); $bw.Write([uint32]$t.Comp); $bw.Write([uint32]$t.Raw) }
$bw.Write([Text.Encoding]::ASCII.GetBytes("AETHPAY1"))   # 8
$bw.Write([uint64]$tableOff)                             # 8
$bw.Write([uint32]$table.Count)                          # 4
$bw.Write([uint32]$files.Count)                          # 4
$bw.Write([uint64]$rawTotal)                             # 8
$bw.Flush(); $fs.Close()

$mb = [math]::Round((Get-Item $outExe).Length / 1MB, 2)
Write-Host ("OK -> $outExe  ({0} MB, {1} files, {2} chunks, {3:N1}x)" -f `
            $mb, $files.Count, $table.Count, ($rawTotal / [double]$compTotal)) -ForegroundColor Green
Write-Host "  silent install:  Aether-Setup.exe /S [/D=C:\path]" -ForegroundColor DarkGray
Write-Host "  silent removal:  uninstall.exe /uninstall /S" -ForegroundColor DarkGray
