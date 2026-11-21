# Builds dist\AetherSetup.exe: stages the files (stage-aether.ps1), builds the uninstaller into them, zips them
# and builds the installer with the zip inside. Uses only what ships with Windows (.NET Framework csc.exe).
#   powershell -File make-installer.ps1 [-NoStage]
param([switch]$NoStage, [string]$OutName = "AetherSetup.exe")
$ErrorActionPreference = 'Stop'
$root = 'C:\Users\Administrator\Desktop\CaelestiaWin'
$here = $PSScriptRoot
$dist = "$root\dist"
$csc  = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
$ico  = "$root\dotfiles\caelestia-launcher\out\caelestia.ico"
$refs = '/r:System.Windows.Forms.dll', '/r:System.Drawing.dll', '/r:System.IO.Compression.dll', '/r:System.IO.Compression.FileSystem.dll', '/r:System.Web.Extensions.dll'

if (-not $NoStage) { & "$here\stage-aether.ps1" }

# uninstaller (no payload) goes into the staged files
& $csc /nologo /target:winexe /optimize+ /define:UNINSTALL "/win32icon:$ico" @refs "/out:$dist\Aether\AetherUninstall.exe" "$here\AetherSetup.cs"
if ($LASTEXITCODE) { throw 'uninstaller build failed' }

# payload
$zip = "$dist\payload.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory("$dist\Aether", $zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)

# installer
& $csc /nologo /target:winexe /optimize+ "/win32icon:$ico" @refs "/resource:$zip,payload.zip" "/out:$dist\$OutName" "$here\AetherSetup.cs"
if ($LASTEXITCODE) { throw 'installer build failed' }
Remove-Item $zip -Force
"built $dist\$OutName  ({0:N0} MB)" -f ((Get-Item "$dist\$OutName").Length / 1MB)
