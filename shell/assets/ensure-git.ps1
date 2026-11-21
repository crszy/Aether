# Finds git, and installs Git for Windows with winget when it is missing (the store and the config installer need it
# for big rice repos: a zip download can't unpack paths past Windows' 260 characters, git can).
# Dot-source it, then:  $git = Ensure-Git   -> the path of git.exe, or $null (no winget / install failed: use a zip)
# Progress goes to the console as plain lines (the Settings window shows the last one).
# Keep this file plain ASCII: Windows PowerShell 5.1 reads a file without a BOM as ANSI.
function Find-Git {
    $c = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    # installed after this process started (PATH not refreshed yet), or never put on PATH
    foreach ($p in @("$env:ProgramFiles\Git\cmd\git.exe", "${env:ProgramFiles(x86)}\Git\cmd\git.exe", "$env:LOCALAPPDATA\Programs\Git\cmd\git.exe")) {
        if ($p -and (Test-Path $p)) { return $p }
    }
    return $null
}

function Ensure-Git {
    $g = Find-Git
    if ($g) { return $g }
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if (-not $winget) {
        [Console]::Out.WriteLine("git is not installed and winget is missing - downloading as a zip instead")
        return $null
    }
    [Console]::Out.WriteLine("Installing git (Git for Windows, via winget) - Windows may ask for permission")
    $bars = '[' + [char]0x2588 + [char]0x2592 + ']'   # winget's progress bar characters
    $ErrorActionPreference = 'Continue'
    & $winget.Source install --id Git.Git -e --source winget --silent --accept-package-agreements --accept-source-agreements --disable-interactivity 2>&1 |
        Where-Object { "$_" -match '\w' -and "$_" -notmatch $bars } |
        ForEach-Object { [Console]::Out.WriteLine("  $_") }
    $g = Find-Git
    if ($g) { [Console]::Out.WriteLine("git installed") } else { [Console]::Out.WriteLine("Could not install git - downloading as a zip instead") }
    return $g
}
