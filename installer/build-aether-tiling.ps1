# Builds AetherWM (Aether's window manager: tiling, Hyprland's config, keybinds and IPC) on THIS PC and turns it
# on in Aether. AetherWM grew out of a fork of komorebi (github.com/LGUG2Z/komorebi), whose license (Komorebi
# License 2.0, LICENSE.md) only allows sharing the source - so Aether carries the source and this script builds it
# here. Started by the installer when you tick the AetherWM box, or later from Aether settings.
#
# No Visual Studio / C++ build tools needed: it builds with Rust's GNU toolchain, which brings its own linker and
# dlltool. That dlltool needs GNU's assembler, which rustup leaves out - so a 5 MB as.exe ships next to this
# script (gnu-as\, see its README) and the dlltool is copied beside it. (LLVM's dlltool was tried instead: its
# import libraries made build scripts crash on start.)
#
#   1. Rust: if it is missing, offers to install it (rustup, from rustup.rs, ~250 MB). Nothing is downloaded without
#      a yes here (or -Yes, when you ticked the box in the installer).
#   2. Builds aetherwm.exe + aetherwmc.exe from the source next to this script (a few minutes the first time).
#   3. Turns it on in Aether, and writes a starter ~/.config/hypr/hyprland.conf if you have none.
param([switch]$Yes,          # -Yes: install Rust without asking (only when you agreed to that already)
      [switch]$BuildOnly,    # -BuildOnly: build and copy to <Out>\bin, but do not turn it on
      [string]$Out = "")     # build folder (default %LOCALAPPDATA%\AetherShell\aetherwm)
$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot
$pkg = (Resolve-Path "$src\..\..").Path
$Host.UI.RawUI.WindowTitle = 'AetherWM - build'
$toolchain = 'stable-x86_64-pc-windows-gnu'

function Say($text, $colour = 'Gray') { Write-Host "  $text" -ForegroundColor $colour }
function Finish($code) { Write-Host ''; if ($code -or -not $Yes) { Read-Host '  Press Enter to close' }; exit $code }

Write-Host ''
Say 'AetherWM - Aether''s window manager' Cyan
Say 'Based on komorebi by LGUG2Z. Komorebi License 2.0: personal use, shared as source, built on your PC.' DarkGray
Write-Host ''

# ---- 1. Rust (GNU toolchain - no Visual Studio) ----
$cargoBin = Join-Path $env:USERPROFILE '.cargo\bin'
if (-not (Get-Command rustup -ErrorAction SilentlyContinue) -and (Test-Path "$cargoBin\rustup.exe")) { $env:PATH = "$cargoBin;$env:PATH" }
if (-not (Get-Command rustup -ErrorAction SilentlyContinue)) {
    Say 'Rust is needed to build AetherWM, and it is not installed.' Yellow
    Say 'It can be installed now from rustup.rs (Rust''s official installer, about 250 MB). No Visual Studio needed.'
    $answer = if ($Yes) { 'y' } else { Read-Host '  Install Rust now? (Y/n)' }
    if ($answer -match '^[nN]') {
        Say 'OK - install it yourself from https://rustup.rs, then run this again from Aether settings.'
        Finish 1
    }
    $installer = Join-Path $env:TEMP 'rustup-init.exe'
    Say 'Downloading rustup-init.exe from https://win.rustup.rs ...' Cyan
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest 'https://win.rustup.rs/x86_64' -OutFile $installer -UseBasicParsing
    } catch {
        Say "Download failed: $_" Red
        Say 'Install Rust from https://rustup.rs yourself, then run this again.'
        Finish 1
    }
    Say 'Installing Rust (GNU toolchain)...' Cyan
    # GNU host: rustup never asks for Visual Studio
    & $installer -y --profile minimal --default-host x86_64-pc-windows-gnu --default-toolchain $toolchain
    if ($LASTEXITCODE) { Say "The Rust installer stopped ($LASTEXITCODE)." Red; Finish 1 }
    $env:PATH = "$cargoBin;$env:PATH"
    if (-not (Get-Command rustup -ErrorAction SilentlyContinue)) { Say 'rustup is still not found - restart your PC and run this again.' Red; Finish 1 }
}
# Rust already there (maybe the MSVC one): add the GNU toolchain next to it; your default stays as it is
Say 'Getting Rust''s GNU toolchain (once)...' Cyan
$ErrorActionPreference = 'Continue'   # rustup reports progress on stderr; PowerShell 5 would call that an error
& rustup toolchain install $toolchain --profile minimal --no-self-update
$ErrorActionPreference = 'Stop'
$rustBin = Join-Path (& rustup run $toolchain rustc --print sysroot).Trim() 'lib\rustlib\x86_64-pc-windows-gnu\bin'
if (-not (Test-Path "$rustBin\self-contained\dlltool.exe")) { Say "Rust's GNU toolchain is incomplete ($rustBin) - check your internet and run this again." Red; Finish 1 }
if (-not (Test-Path "$src\gnu-as\as.exe")) { Say "gnu-as\as.exe is missing next to this script - reinstall Aether." Red; Finish 1 }

# ---- 2. build ----
if (-not (Test-Path "$src\Cargo.toml") -and (Test-Path "$src\source.zip")) {
    Say 'Unpacking the source...' Cyan
    # .NET directly (Expand-Archive differs between PowerShell 5 and 7 and broke when both were around);
    # entry by entry with overwrite, so a half-finished earlier unpack is fine too
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead("$src\source.zip")
    try {
        foreach ($entry in $zip.Entries) {
            $target = Join-Path $src $entry.FullName
            if ($entry.FullName.EndsWith('/') -or $entry.FullName.EndsWith('\')) { New-Item -ItemType Directory -Force $target | Out-Null; continue }
            New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        }
    } finally {
        $zip.Dispose()
    }
}
# half the cores: building on all of them can make the PC unresponsive
$jobs = [Math]::Max(1, [int]([Environment]::ProcessorCount / 2))
# the build lives outside the program folder: updating Aether (which replaces that folder) keeps the window manager
# working, and the next build only redoes what changed
$out = if ($Out) { $Out } else { Join-Path $env:LOCALAPPDATA 'AetherShell\aetherwm' }
New-Item -ItemType Directory -Force "$out\tools" | Out-Null
# GNU dlltool runs the "as" in its own folder: put rustup's dlltool next to the bundled assembler
Copy-Item "$src\gnu-as\*" "$out\tools\" -Force
Copy-Item "$rustBin\self-contained\dlltool.exe" "$out\tools\dlltool.exe" -Force
$env:RUSTFLAGS = "-C dlltool=$out\tools\dlltool.exe"
$env:PATH = "$rustBin\self-contained;$env:PATH"   # GNU linker bits that ship with the toolchain
Push-Location $src
try {
    Say "Building AetherWM with $jobs jobs (the first build takes a few minutes)..." Cyan
    $ErrorActionPreference = 'Continue'   # cargo prints progress on stderr; the exit code decides
    & cargo "+$toolchain" build --release -p komorebi -p komorebic -j $jobs --target-dir "$out\target"
    if ($LASTEXITCODE) { throw "cargo failed ($LASTEXITCODE)" }
} catch {
    Say "Build failed: $_" Red
    Finish 1
} finally {
    Pop-Location
}
$built = "$out\target\release"
if (-not (Test-Path "$built\aetherwm.exe")) { Say 'aetherwm.exe was not made - see the messages above.' Red; Finish 1 }
# the running copy lives in its own folder: rebuilding while AetherWM runs must not fail on a locked exe
$bin = "$out\bin"
New-Item -ItemType Directory -Force $bin | Out-Null
$running = Get-Process aetherwm -ErrorAction SilentlyContinue
foreach ($exe in 'aetherwm.exe', 'aetherwmc.exe') {
    try { Copy-Item "$built\$exe" "$bin\$exe" -Force } catch { Say "$exe is in use - it updates after AetherWM restarts." Yellow }
}
Copy-Item "$src\LICENSE.md" "$bin\LICENSE.md" -Force -ErrorAction SilentlyContinue
$binFwd = ($bin -replace '\\', '/')

if ($BuildOnly) { Say "Built: $bin (not turned on: -BuildOnly)." Green; Finish 0 }

# ---- 3. turn it on ----
$conf = Join-Path $env:USERPROFILE '.config\hypr\hyprland.conf'
if (-not (Test-Path $conf) -and (Test-Path "$src\starter-hyprland.conf")) {
    New-Item -ItemType Directory -Force (Split-Path $conf) | Out-Null
    Copy-Item "$src\starter-hyprland.conf" $conf
    Say "Wrote a starter $conf (Alt+H/J/K/L focus, Alt+Shift moves, Alt+1-5 workspaces, Alt+O overview)." Green
}

# the setting is still called "komorebi" inside windows.json (older Aether versions read that key)
$settings = @{ komorebi = $binFwd; hyprlandWm = $true; hyprlandAlt = $true }
$qs = Join-Path $pkg 'qs\qs.exe'
$shell = Join-Path $env:USERPROFILE '.config\quickshell\aether\shell.qml'   # your copy of Aether's config (Caelestia.exe makes it)
if (-not (Test-Path $shell)) { $shell = Join-Path $pkg 'config\shell.qml' }
if (Get-Process qsw -ErrorAction SilentlyContinue) {
    # the running Aether applies them at once (and starts the window manager)
    foreach ($k in 'hyprlandAlt', 'hyprlandWm', 'komorebi') { & $qs ipc -p $shell call windows set $k "$($settings[$k])" | Out-Null }
} else {
    # Aether is not running: write them into its settings file; they apply when it starts
    $file = Join-Path $env:USERPROFILE '.config\caelestia\windows.json'
    $json = if (Test-Path $file) { Get-Content $file -Raw | ConvertFrom-Json } else { [pscustomobject]@{} }
    foreach ($k in $settings.Keys) { $json | Add-Member -NotePropertyName $k -NotePropertyValue $settings[$k] -Force }
    New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
    [IO.File]::WriteAllText($file, ($json | ConvertTo-Json -Depth 20), (New-Object Text.UTF8Encoding $false))
}

Write-Host ''
Say "Done. AetherWM is on ($bin)." Green
Say 'Keys: see ~/.config/hypr/hyprland.conf. Turn it off: Aether settings > Hyprland.'
Finish 0
