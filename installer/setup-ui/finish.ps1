# Last step of Aether's setup (run by setup-ui/shell.qml after the files are copied): shortcuts, start with
# Windows, and the Settings > Apps entry. Current user only, no administrator.
param([string]$Dir, [string]$StartMenu = '1', [string]$Desktop = '0', [string]$Autostart = '0')
$ErrorActionPreference = 'Stop'
$exe = Join-Path $Dir 'Caelestia.exe'
if (-not (Test-Path $exe)) { exit 2 }
$shell = New-Object -ComObject WScript.Shell

function Link($path, $target, $arguments, $description) {
    $l = $shell.CreateShortcut($path)
    $l.TargetPath = $target
    $l.Arguments = $arguments
    $l.WorkingDirectory = Split-Path $target
    $l.IconLocation = "$exe,0"
    $l.Description = $description
    $l.Save()
}

$menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Aether Shell'
if ($StartMenu -eq '1') {
    New-Item -ItemType Directory -Force $menu | Out-Null
    Link (Join-Path $menu 'Aether Shell.lnk') $exe '' 'Aether Shell - Linux shells on Windows'
    Link (Join-Path $menu 'Aether Shell settings.lnk') $exe '--settings' 'Aether Shell settings'
    $un = Join-Path $Dir 'AetherUninstall.exe'
    if (Test-Path $un) { Link (Join-Path $menu 'Uninstall Aether Shell.lnk') $un '' 'Remove Aether Shell' }
}
if ($Desktop -eq '1') {
    Link (Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Aether Shell.lnk') $exe '' 'Aether Shell - Linux shells on Windows'
}

$run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if ($Autostart -eq '1') { Set-ItemProperty $run -Name 'Caelestia' -Value "`"$exe`"" }
else { Remove-ItemProperty $run -Name 'Caelestia' -ErrorAction SilentlyContinue }

$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\AetherShell'  # not "Aether": another program uses it
New-Item -Path $key -Force | Out-Null
$size = [int]((Get-ChildItem $Dir -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum / 1KB)
$values = @{ DisplayName = 'Aether Shell'; DisplayIcon = $exe; DisplayVersion = '1.0'; Publisher = 'Aether Shell'; InstallLocation = $Dir;
             UninstallString = "`"$(Join-Path $Dir 'AetherUninstall.exe')`"" }
foreach ($k in $values.Keys) { Set-ItemProperty $key -Name $k -Value $values[$k] }
New-ItemProperty $key -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
New-ItemProperty $key -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null
New-ItemProperty $key -Name EstimatedSize -Value $size -PropertyType DWord -Force | Out-Null
exit 0
