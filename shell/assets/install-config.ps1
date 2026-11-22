# Installs a Quickshell config from a link into ~/.config/quickshell/<name> - how rices are shared on Linux
# (a git repo you clone into ~/.config/quickshell) - and updates it later without losing your edits.
# Used by Aether Settings > Quickshell configs.
#   install-config.ps1 -Url <git repo | GitHub page | .zip link> [-Name <folder name>] [-Launcher <Caelestia.exe>]
#   install-config.ps1 -Update -Name <folder name> [-Launcher <Caelestia.exe>]
# Getting it: git clone when git is installed; otherwise GitHub repos and .zip links are downloaded with Windows' own
# curl + tar. The folder holding the shallowest shell.qml is the config (many rice repos keep it in a sub-folder).
# Two kinds of install, remembered in <config>\.aether\source.json:
#   "git"      the repo root IS the config: it stays a git clone; update = git pull (your changes kept: --autostash)
#   "download" anything else: copied in through Caelestia.exe --sync-from, which remembers what it delivered
#              (.aether\base.tsv); update = fetch again + the same merge as Aether's own config (your edits kept,
#              both changed -> <file>.aether-new)
# Prints "OK <name>" or "ERROR <what went wrong>" as its last line.
param([string]$Url = "", [string]$Name = "", [switch]$Update, [string]$Launcher = "")
# public repos only: never pop up a GitHub sign-in (a missing/private repo just fails)
$env:GIT_TERMINAL_PROMPT = '0'; $env:GCM_INTERACTIVE = 'never'
$ErrorActionPreference = 'Stop'
function Fail($m) { [Console]::Out.WriteLine("ERROR $m"); exit 1 }   # straight out: works inside a captured call too

$root = Join-Path $env:USERPROFILE '.config\quickshell'
New-Item -ItemType Directory -Force $root | Out-Null
. (Join-Path $PSScriptRoot 'ensure-git.ps1')   # Ensure-Git: finds git, or installs it with winget
$script:gitExe = $null
function Get-Git { if (-not $script:gitExe) { $script:gitExe = Ensure-Git }; return $script:gitExe }

function Run-Git([string[]]$a) {
    $ErrorActionPreference = 'Continue'   # git writes its progress to stderr; that is not an error
    & (Get-Git) @a 2>&1 | ForEach-Object { Write-Host "$_" }
    return $LASTEXITCODE
}

# Checks out a clone made with --no-checkout. Linux repos can hold file names Windows can't save (':' '?' '*' ...),
# which would make the whole checkout fail: those files are left out (sparse checkout) and listed. Then submodules.
function Checkout-Safe([string]$repo) {
    $bad = @(& (Get-Git) -C $repo ls-tree -r --name-only HEAD | Where-Object { @($_ -split '/' | Where-Object { $_ -match '[:*?"<>|]|[ .]$' }).Count })
    if ($bad.Count) {
        Write-Host "Skipping $($bad.Count) file(s) whose names Windows can't save, e.g. $($bad[0])"
        # git refuses to even list such names in its index unless protectNTFS is off; the sparse rules below keep them
        # from ever being written to disk (where ':' would become a hidden data stream).
        Run-Git @('-C', $repo, 'config', 'core.protectNTFS', 'false') | Out-Null
        $patterns = @('/*') + ($bad | ForEach-Object { '!/' + ($_ -replace '([\[\]*?!#\\])', '\$1') })
        if ((Run-Git (@('-C', $repo, 'sparse-checkout', 'set', '--no-cone') + $patterns)) -ne 0) { Fail "git sparse-checkout failed" }
    }
    if ((Run-Git @('-C', $repo, 'checkout', '-q')) -ne 0) { Fail "git checkout failed" }
    if (Test-Path (Join-Path $repo '.gitmodules')) {
        if ((Run-Git @('-C', $repo, 'submodule', 'update', '--init', '--depth', '1')) -ne 0) { Write-Host "Some submodules could not be fetched" }
    }
}

# git on Windows (without developer mode) checks symlinks out as small text files holding the target path. Replace
# each with a copy of what it points to, so configs that link shared folders (like Dux Shell's DCommon) still load.
function Resolve-Links([string]$repo) {
    $top = (Get-Item -LiteralPath $repo).FullName   # same spelling as $from (8.3 names expanded)
    $copied = 0; $left = 0
    $rels = @(& (Get-Git) -C $repo ls-files -s 2>$null | Where-Object { $_ -match '^120000 ' } | ForEach-Object { ($_ -split "`t", 2)[1] })
    $pending = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($r in $rels) { [void]$pending.Add((Join-Path $top ($r -replace '/', '\'))) }
    # a link to a link: copy in rounds (the target link is replaced by its copy first)
    for ($round = 0; $round -lt 4; $round++) {
        $changed = 0
        foreach ($rel in $rels) {
            $link = Join-Path $top ($rel -replace '/', '\')
            if (-not $pending.Contains($link)) { continue }
            $item = Get-Item -LiteralPath $link -Force -ErrorAction SilentlyContinue
            if (-not $item -or $item.PSIsContainer -or $item.LinkType -or $item.Length -gt 1024) { continue }   # missing, a real link, or already copied
            $target = (Get-Content -Raw -LiteralPath $link).Trim()
            if (-not $target -or $target -match '[\x00-\x1f]') { continue }   # not a path: already a copy
            $from = Get-Item -LiteralPath (Join-Path (Split-Path $link) ($target -replace '/', '\')) -Force -ErrorAction SilentlyContinue
            if (-not $from -or -not $from.FullName.StartsWith($top, [StringComparison]::OrdinalIgnoreCase)) {
                if ($round -eq 0) { $left++ }; continue
            }
            if ($pending.Contains($from.FullName)) { continue }   # points at a link not copied yet: next round
            Remove-Item -LiteralPath $link -Force
            Copy-Item -LiteralPath $from.FullName -Destination $link -Recurse -Force
            [void]$pending.Remove($link)
            $copied++; $changed++
        }
        if ($changed -eq 0) { break }
    }
    if ($copied) { Write-Host "Replaced $copied Linux link(s) with copies" }
    if ($left) { Write-Host "$left link(s) point outside the download - left as they are" }
}

# fetch $Url into $tmp\src; returns @{ dir = <folder with shell.qml>; isRoot = <that folder is the git clone root> }
function Fetch([string]$Url, [string]$tmp, [bool]$allowGit) {
    $gh = $null
    if ($Url -match '^https?://github\.com/([^/]+)/([^/#?]+?)(\.git)?(/tree/([^/]+)(/.*)?)?/?$') {
        $gh = @{ owner = $Matches[1]; repo = $Matches[2]; branch = $Matches[5]; sub = $Matches[6] }
    }
    $src = Join-Path $tmp 'src'
    $cloned = $false
    $isZip = $Url -match '\.zip([?#].*)?$'
    $git = if (-not $isZip -and $allowGit) { Get-Git } else { $null }   # installs git when missing
    if ($isZip -or ($gh -and -not $git)) {
        $zipUrl = if ($gh) { "https://codeload.github.com/$($gh.owner)/$($gh.repo)/zip/" + $(if ($gh.branch) { "refs/heads/$($gh.branch)" } else { 'HEAD' }) } else { $Url }
        Write-Host "Downloading $zipUrl"
        $zip = Join-Path $tmp 'config.zip'
        & "$env:SystemRoot\System32\curl.exe" -fsSL -o $zip $zipUrl
        if ($LASTEXITCODE -ne 0) { Fail "Download failed (curl exit $LASTEXITCODE)" }
        New-Item -ItemType Directory -Force $src | Out-Null
        & "$env:SystemRoot\System32\tar.exe" -xf $zip -C $src
        if ($LASTEXITCODE -ne 0) { Fail "Could not unpack the zip" }
    } elseif ($git) {
        $cloneUrl = if ($gh) { "https://github.com/$($gh.owner)/$($gh.repo).git" } else { $Url }
        Write-Host "git clone $cloneUrl"
        $a = @('-c', 'core.longpaths=true', 'clone', '--depth', '1', '--no-checkout')
        if ($gh -and $gh.branch) { $a += @('--branch', $gh.branch) }
        if ((Run-Git ($a + @($cloneUrl, $src))) -ne 0) { Fail "git clone failed" }
        Run-Git @('-C', $src, 'config', 'core.longpaths', 'true') | Out-Null   # kept for later pulls
        Checkout-Safe $src
        Resolve-Links $src
        $cloned = $true
    } else {
        Fail "git could not be installed - use a GitHub link or a .zip link"
    }
    $look = $src
    if ($gh -and $gh.sub) {
        $inner = Get-ChildItem $src -Directory | Select-Object -First 1   # zips have one top folder
        $base = if (Test-Path (Join-Path $src $gh.sub.TrimStart('/'))) { $src } elseif ($inner) { $inner.FullName } else { $src }
        $look = Join-Path $base $gh.sub.TrimStart('/')
    }
    $shell = Get-ChildItem $look -Recurse -Filter shell.qml -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '\\\.git\\' } |
        Sort-Object { $_.FullName.Split('\').Count } | Select-Object -First 1
    if (-not $shell) { Fail "No shell.qml in it - that is not a Quickshell config" }
    $dir = $shell.Directory.FullName
    return @{ dir = $dir; isRoot = ($cloned -and (Test-Path (Join-Path $dir '.git'))) }
}

# copy a fetched folder into ~/.config/quickshell/<name> through the launcher's merge (keeps your edits)
function Merge-In([string]$from, [string]$name) {
    if (-not $Launcher -or -not (Test-Path $Launcher)) { Fail "Caelestia.exe not found - start Aether from Caelestia.exe" }
    $gitDir = Join-Path $from '.git'
    if (Test-Path $gitDir) { Remove-Item -Recurse -Force $gitDir }   # a sub-folder install is not a clone of its own
    $p = Start-Process -FilePath $Launcher -ArgumentList @('--sync-from', "`"$from`"", $name) -Wait -PassThru -WindowStyle Hidden
    if ($p.ExitCode -ne 0) { Fail "Copying it in failed" }
    $report = Join-Path $root "$name\.aether\last-update.txt"
    if (Test-Path $report) { Get-Content $report | Select-Object -Skip 1 -First 1 | ForEach-Object { Write-Host $_ } }
}

function Save-Source([string]$name, [string]$url, [string]$kind) {
    $meta = Join-Path $root "$name\.aether"
    New-Item -ItemType Directory -Force $meta | Out-Null
    $json = @{ url = $url; kind = $kind; installed = (Get-Date).ToString('s') } | ConvertTo-Json
    [IO.File]::WriteAllText((Join-Path $meta 'source.json'), $json, (New-Object Text.UTF8Encoding $false))
    if ($kind -eq 'git') {   # keep Aether's notes out of the repo
        $ex = Join-Path $root "$name\.git\info\exclude"
        if ((Test-Path $ex) -and -not (Select-String -Path $ex -SimpleMatch '.aether/' -Quiet)) { Add-Content $ex '.aether/' }
    }
}

# {cpu} {gpu} {ram} {os} {osVersion} {user} {userPicture} in a patch's replace: this PC's own (Linux shells show
# placeholders like "CPU Name" / "/home/username/..." where their install script would have asked)
$script:tokens = $null
function Expand-Tokens([string]$s) {
    if ($s -notmatch '\{(cpu|gpu|ram|os|osVersion|user|userPicture)\}') { return $s }
    if (-not $script:tokens) {
        $os = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
        $pic = @(@(Get-ChildItem "$env:APPDATA\Microsoft\Windows\AccountPictures" -Include *.png, *.jpg -Recurse -ErrorAction SilentlyContinue | Sort-Object Length -Descending) + @(Get-Item "$env:ProgramData\Microsoft\User Account Pictures\user.png" -ErrorAction SilentlyContinue) | Select-Object -First 1)   # yours, else Windows' default
        $script:tokens = @{
            cpu = ((Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue | Select-Object -First 1).Name -replace '\s+', ' ').Trim()
            gpu = (Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue | Where-Object { $_.Name -notmatch 'Basic|Remote|Virtual' } | Select-Object -First 1).Name
            ram = "$([math]::Round(((Get-CimInstance Win32_ComputerSystem -ErrorAction SilentlyContinue).TotalPhysicalMemory) / 1GB)) GB"
            os = ($os.Caption -replace '^Microsoft\s+', '')
            osVersion = "$($os.Version)"
            user = $env:USERNAME
            userPicture = $(if ($pic) { $pic[0].FullName -replace '\\', '/' } else { '' })
        }
    }
    foreach ($k in $script:tokens.Keys) { $s = $s.Replace("{$k}", ([string]$script:tokens[$k]).Replace('"', "'")) }
    return $s
}
# Small Windows fixes for known shells (assets/shell-patches.json, by repo link): each is a "find" -> "replace" in
# one file, applied after every download / pull. Already applied or changed by the author = left alone.
function Apply-Patches([string]$dir, [string]$url) {
    $file = Join-Path $PSScriptRoot 'shell-patches.json'
    if (-not (Test-Path $file)) { return }
    $key = ($url.ToLower() -replace '[?#].*$', '' -replace '\.git$', '' -replace '/+$', '')
    $entry = @((Get-Content -Raw $file | ConvertFrom-Json).shells | Where-Object { ($_.url.ToLower() -replace '/+$', '') -eq $key }) | Select-Object -First 1
    $list = @(if ($entry) { $entry.patches })
    # keys Linux gets from Hyprland's config (Super -> the shell's launcher): Aether's Quickshell reads them from
    # <config>/defaults/windows/keybinds.json; Super+Escape always goes back to Aether. A file there already is kept.
    # a shell with no wallpaper of its own ("wallpaper": true): AetherWallpaper.qml next to its shell.qml shows yours
    # (one of its patches puts AetherWallpaper {} in shell.qml)
    if ($entry -and $entry.wallpaper) {
        $wp = Join-Path $dir 'AetherWallpaper.qml'
        if (-not (Test-Path -LiteralPath $wp)) { Copy-Item (Join-Path $PSScriptRoot 'AetherWallpaper.qml') $wp; Write-Output 'Windows fix: your Aether wallpaper under it (it has none of its own)' }
    }
    $kb = Join-Path $dir 'defaults\windows\keybinds.json'
    if ($entry -and $entry.keybinds -and -not (Test-Path -LiteralPath $kb)) {
        $binds = @($entry.keybinds)
        if ($Launcher) { $binds += [pscustomobject]@{ keys = 'Super+Escape'; action = 'exec'; command = "`"$Launcher`" --config aether"; description = 'back to Aether' } }
        New-Item -ItemType Directory -Force (Split-Path $kb) | Out-Null
        [IO.File]::WriteAllText($kb, ([pscustomobject]@{ binds = $binds } | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding $false))
        Write-Output "Windows keys: $((@($entry.keybinds) | ForEach-Object { "$($_.keys) = $($_.description)" }) -join ', ')$(if ($Launcher) { ', Super+Escape = back to Aether' })"
    }
    foreach ($p in $list) {
        $f = Join-Path $dir ($p.file -replace '/', '\')
        if (-not (Test-Path -LiteralPath $f)) { Write-Output "Windows fix skipped ($($p.file) is gone): $($p.why)"; continue }
        $text = [IO.File]::ReadAllText($f)
        $find = $p.find; $repl = Expand-Tokens $p.replace
        if ($text.Contains("`r`n")) { $find = $find -replace "`r?`n", "`r`n"; $repl = $repl -replace "`r?`n", "`r`n" }
        if ($text.Contains($repl)) { continue }   # already there
        if (-not $text.Contains($find)) { Write-Output "Windows fix skipped (the file changed upstream): $($p.why)"; continue }
        $i = $text.IndexOf($find)
        [IO.File]::WriteAllText($f, $text.Substring(0, $i) + $repl + $text.Substring($i + $find.Length), (New-Object Text.UTF8Encoding $false))
        Write-Output "Windows fix: $($p.why)"
    }
}

$tmp = Join-Path $env:TEMP ("aether-config-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    if ($Update) {
        $dest = Join-Path $root $Name
        $sourceFile = Join-Path $dest '.aether\source.json'
        if (-not $Name -or -not (Test-Path $sourceFile)) { Fail "$Name was not installed from a link - nothing to update from" }
        $source = Get-Content $sourceFile -Raw | ConvertFrom-Json
        if ($source.kind -eq 'git') {
            if (-not (Get-Git)) { Fail "This config is a git clone and git could not be installed" }
            Write-Output "git pull"
            if ((Run-Git @('-C', $dest, 'pull', '--ff-only', '--autostash')) -ne 0) { Fail "git pull failed - your changes are untouched" }
            Apply-Patches $dest $source.url
        } else {
            $got = Fetch $source.url $tmp $false
            Apply-Patches $got.dir $source.url
            Merge-In $got.dir $Name
        }
        Write-Output "OK $Name"
        exit 0
    }

    $Url = $Url.Trim()
    if ($Url -notmatch '^https?://') { Fail "That is not a web link (https://...)" }
    if (-not $Name) {
        $Name = ($Url -replace '[?#].*$', '' -replace '/+$', '' -replace '\.git$', '' -replace '\.zip$', '').Split('/')[-1]
        if ($Url -match 'github\.com/[^/]+/[^/]+/tree/[^/]+/(.+)$') { $Name = ($Matches[1] -replace '/+$', '').Split('/')[-1] }
    }
    $Name = ($Name -replace '[^\w.-]', '-').Trim('.-')
    if (-not $Name) { Fail "Could not make a folder name from the link - give one" }
    $dest = Join-Path $root $Name
    if (Test-Path $dest) { Fail "~/.config/quickshell/$Name already exists - pick another name or remove that folder" }

    $got = Fetch $Url $tmp $true
    Apply-Patches $got.dir $Url
    if ($got.isRoot) {
        Move-Item $got.dir $dest
        Save-Source $Name $Url 'git'
    } else {
        Merge-In $got.dir $Name
        Save-Source $Name $Url 'download'
    }
    Write-Output "Installed into ~/.config/quickshell/$Name"
    Write-Output "OK $Name"
} catch {
    Fail $_.Exception.Message
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
