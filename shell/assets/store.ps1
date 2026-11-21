# AetherStore engine: get a rice (any Linux dotfiles repo, a Windows rice, or an Aether rice) and put each part where
# it lives on Windows - with a backup of everything it replaces, so uninstall puts your setup back.
#   store.ps1 scan -Url <git repo | GitHub link | .zip>      -> fetches into the store cache, prints PARTS <json>
#   store.ps1 install -Id <id> [-Parts hypr,fastfetch,...]   -> installs those parts (default: all), prints OK <id>
#   store.ps1 uninstall -Id <id>                             -> restores the backups, removes what it added
#   store.ps1 list                                           -> INSTALLED <json>
# Parts are found by folder name, at the top of the repo or under .config/ / config/ / dotfiles/ / dots/ (Linux layout):
#   hypr -> AetherWM (~/.config/hypr; exec lines are Linux programs, so they are commented out)
#   caelestia, noctalia, ryoku, DankMaterialShell / dms, illogical-impulse -> the settings of Aether's ports
#   quickshell/<name> -> a Quickshell config (~/.config/quickshell/<rice>-<name>)
#   kitty / alacritty / foot -> a Windows Terminal profile with its font, size, opacity, padding, cursor and colours (a scheme;
#     a rice that makes its colours from the wallpaper gets Aether's live "Aether" scheme) - alacritty's config also as is
#   fastfetch, starship.toml, cava, waybar, fish, wezterm -> ~/.config/...
#   a "chrome" folder with userChrome.css -> Zen (and Firefox, when installed) profiles
#   wallpapers / walls / wallpaper / backgrounds -> your wallpaper folder/<rice>
#   Windows Terminal settings.json with "schemes" -> its schemes merged in
#   aether/ (lock, osd, panels, widgets, desktop, dashboard, mods, skins) -> ~/.config/caelestia/user/...
#   a folder with hyprlock.conf (one per style in style packs) -> ~/.config/hypr (Aether's Hyprlock lock screen draws it);
#     its fonts (.ttf/.otf) are installed for you (per user, HKCU - put back on uninstall)
# Several parts of one type (hyprlock styles) get keys "type:name"; -Parts takes keys or types.
# An aether-store.json at the top can name the rice and list parts itself: { "name", "parts": [ { "type", "path" } ] }.
# Prints progress lines, then "OK ..." / "PARTS {json}" / "INSTALLED [json]", or "ERROR <what went wrong>".
#   -Hint <base64 of the json parts> -Name <name>: the browse list says which parts a repo has (same shape as aether-store.json parts:
#   [ { "type", "path", "into" } ] - "into" = a sub-folder of the target, used by fastfetch presets)
param([Parameter(Position = 0)][string]$Action = "list", [string]$Url = "", [string]$Id = "", [string]$Parts = "", [string]$Hint = "", [string]$Name = "")
# public repos only: never pop up a GitHub sign-in (a missing/private repo just fails)
$env:GIT_TERMINAL_PROMPT = '0'; $env:GCM_INTERACTIVE = 'never'
$ErrorActionPreference = 'Stop'
function Fail($m) { [Console]::Out.WriteLine("ERROR $m"); exit 1 }
function Say($m) { [Console]::Out.WriteLine($m) }

$home_ = $env:USERPROFILE
$cfg = Join-Path $home_ '.config'
$store = Join-Path $env:LOCALAPPDATA 'AetherShell\store'
$cache = Join-Path $store 'cache'
$backups = Join-Path $store 'backups'
$installedFile = Join-Path $store 'installed.json'
New-Item -ItemType Directory -Force $cache, $backups | Out-Null
. (Join-Path $PSScriptRoot 'ensure-git.ps1')   # Ensure-Git: finds git, or installs it with winget
$script:gitExe = $null
$utf8 = New-Object Text.UTF8Encoding $false

# long paths: rice repos nest past Windows' 260 characters, which Windows PowerShell's own cmdlets can't reach -
# every file walk / copy / delete goes through .NET with the \\?\ prefix
function LongPath([string]$p) { if ($p.StartsWith('\\?\')) { $p } else { '\\?\' + [IO.Path]::GetFullPath($p) } }
function ShortPath([string]$p) { if ($p.StartsWith('\\?\')) { $p.Substring(4) } else { $p } }
function Files([string]$dir) {   # every file under dir, .git skipped
    if (-not [IO.Directory]::Exists((LongPath $dir))) { return @() }
    $git_ = [IO.Path]::DirectorySeparatorChar + '.git' + [IO.Path]::DirectorySeparatorChar
    return @([IO.Directory]::EnumerateFiles((LongPath $dir), '*', [IO.SearchOption]::AllDirectories) | ForEach-Object { ShortPath $_ } | Where-Object { -not $_.Contains($git_) })
}
function Dirs([string]$dir) {
    if (-not [IO.Directory]::Exists((LongPath $dir))) { return @() }
    return @([IO.Directory]::EnumerateDirectories((LongPath $dir)) | ForEach-Object { ShortPath $_ })
}
function Exists([string]$p) { [IO.File]::Exists((LongPath $p)) }
function Remove-Tree([string]$dir) {
    if (-not [IO.Directory]::Exists((LongPath $dir))) { return }
    # git's pack files are read-only, and a read-only file stops a recursive delete
    foreach ($f in [IO.Directory]::EnumerateFiles((LongPath $dir), '*', [IO.SearchOption]::AllDirectories)) { [IO.File]::SetAttributes($f, [IO.FileAttributes]::Normal) }
    [IO.Directory]::Delete((LongPath $dir), $true)
}
function Make-Dir([string]$dir) { [void][IO.Directory]::CreateDirectory((LongPath $dir)) }
function Copy-One([string]$src, [string]$dst) { Make-Dir ([IO.Path]::GetDirectoryName($dst)); [IO.File]::Copy((LongPath $src), (LongPath $dst), $true) }
function Read-Text([string]$p) { [IO.File]::ReadAllText((LongPath $p), [Text.Encoding]::UTF8) }
function Write-Text([string]$p, [string]$t) { Make-Dir ([IO.Path]::GetDirectoryName($p)); [IO.File]::WriteAllText((LongPath $p), $t, $utf8) }
function Rel([string]$root, [string]$p) {   # .NET hands back expanded names (C:\Users\Administrator, not ADMINI~1)
    $r = ShortPath (LongPath $root)
    $cut = if ($p.StartsWith($r, [StringComparison]::OrdinalIgnoreCase)) { $r.Length } else { $root.Length }
    $p.Substring($cut).TrimStart([IO.Path]::DirectorySeparatorChar)
}
function Read-Json($p) { if (Exists $p) { Read-Text $p | ConvertFrom-Json } else { $null } }
function Write-Json($p, $o) { Write-Text $p ($o | ConvertTo-Json -Depth 20) }

# ---------------------------------------------------------------- fetching
function Fetch([string]$Url, [string]$dest) {
    $gh = $null
    if ($Url -match '^https?://github\.com/([^/]+)/([^/#?]+?)(\.git)?(/tree/([^/]+)(/.*)?)?/?$') {
        $gh = @{ owner = $Matches[1]; repo = $Matches[2]; branch = $Matches[5] }
    }
    $isZip = $Url -match '\.zip([?#].*)?$'
    $isLocal = Test-Path -LiteralPath $Url
    if (-not $isLocal -and -not $isZip -and -not $script:gitExe) { $script:gitExe = Ensure-Git }   # installs git when missing
    if ($isLocal) {   # a rice you downloaded: a folder or a .zip on this PC
        Make-Dir $dest
        if ((Get-Item -LiteralPath $Url).PSIsContainer) {
            $srcRoot = ShortPath (LongPath $Url)
            foreach ($f in Files $srcRoot) { Copy-One $f (Join-Path $dest (Rel $srcRoot $f)) }
        } else {
            & "$env:SystemRoot\System32\tar.exe" -xf $Url -C $dest
            if ($LASTEXITCODE -ne 0) { Say "(some files could not be unpacked)" }
        }
    } elseif ($isZip -or ($gh -and -not $script:gitExe)) {
        $zipUrl = if ($gh) { "https://codeload.github.com/$($gh.owner)/$($gh.repo)/zip/" + $(if ($gh.branch) { "refs/heads/$($gh.branch)" } else { 'HEAD' }) } else { $Url }
        Say "Downloading $zipUrl"
        $zip = "$dest.zip"
        & "$env:SystemRoot\System32\curl.exe" -fsSL -o $zip $zipUrl
        if ($LASTEXITCODE -ne 0) { Fail "Download failed (curl exit $LASTEXITCODE)" }
        Make-Dir $dest
        & "$env:SystemRoot\System32\tar.exe" -xf $zip -C $dest
        $code = $LASTEXITCODE
        [IO.File]::Delete($zip)
        if ($code -ne 0) { Say "(some files could not be unpacked - their paths are too long for a zip)" }
    } elseif ($script:gitExe) {
        $cloneUrl = if ($gh) { "https://github.com/$($gh.owner)/$($gh.repo).git" } else { $Url }
        Say "git clone $cloneUrl"
        $a = @('-c', 'core.longpaths=true', 'clone', '--depth', '1')
        if ($gh -and $gh.branch) { $a += @('--branch', $gh.branch) }
        $ErrorActionPreference = 'Continue'   # git writes its progress to stderr
        & $script:gitExe @a $cloneUrl $dest 2>&1 | ForEach-Object { Say "$_" }
        $code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($code -ne 0) { Fail "git clone failed" }
    } else {
        Fail "git could not be installed - use a GitHub link or a .zip link"
    }
    $top = @(Dirs $dest)
    if ($top.Count -eq 1 -and @([IO.Directory]::EnumerateFiles((LongPath $dest))).Count -eq 0) { return $top[0] }   # a zip's one top folder
    return $dest
}

# ---------------------------------------------------------------- finding the parts
$wtSettings = Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json'
function Browser-Profiles {
    $out = @()
    foreach ($app in @('zen', 'Mozilla\Firefox')) {
        $ini = Join-Path $env:APPDATA "$app\installs.ini"
        if (-not (Test-Path $ini)) { $ini = Join-Path $env:APPDATA "$app\profiles.ini" }
        if (-not (Test-Path $ini)) { continue }
        $m = Select-String -Path $ini -Pattern '^(Default|Path)=(.+)$' | Select-Object -First 1
        if ($m) { $out += @{ app = ($app -replace '.*\\', ''); dir = (Join-Path (Join-Path $env:APPDATA $app) ($m.Matches[0].Groups[2].Value.Trim() -replace '/', '\')) } }
    }
    return $out
}

$byName = @{
    'hypr' = 'hypr'; 'caelestia' = 'caelestia'; 'noctalia' = 'noctalia'; 'ryoku' = 'ryoku'
    'dankmaterialshell' = 'dms'; 'dms' = 'dms'; 'illogical-impulse' = 'end4'
    'kitty' = 'kitty'; 'alacritty' = 'alacritty'; 'foot' = 'foot'; 'wezterm' = 'wezterm'
    'fastfetch' = 'fastfetch'; 'cava' = 'cava'; 'waybar' = 'waybar'; 'fish' = 'fish'
    'wallpapers' = 'wallpapers'; 'walls' = 'wallpapers'; 'wallpaper' = 'wallpapers'; 'backgrounds' = 'wallpapers'
}
$labels = @{
    hypr = 'Hyprland config -> AetherWM'; caelestia = 'Caelestia settings'; noctalia = 'Noctalia settings'; ryoku = 'Ryoku settings'
    dms = 'DankMaterialShell settings'; end4 = 'end-4 (illogical-impulse) settings'; quickshell = 'Quickshell config (a shell)'
    kitty = 'kitty look -> Windows Terminal profile'; foot = 'foot look -> Windows Terminal profile'; alacritty = 'Alacritty config + Windows Terminal profile'
    wezterm = 'WezTerm config'; fastfetch = 'fastfetch config'; starship = 'Starship prompt'; cava = 'cava config'; waybar = 'Waybar config'
    fish = 'fish config'; wallpapers = 'Wallpapers'; chrome = 'Browser theme (Zen / Firefox userChrome)'; wt = 'Windows Terminal colour schemes'
    wtscheme = 'Windows Terminal colour scheme(s)'; hyprlock = 'Hyprlock lock screen'
    aether = 'Aether pieces (lock screens, panels, widgets...)'
}

function Find-Parts([string]$root) {
    $parts = New-Object System.Collections.ArrayList
    $seen = @{}
    $add = { param($t, $path, $name) [void]$parts.Add([pscustomobject]@{ type = $t; path = $path; name = $name; label = $labels[$t] }) }
    $bases = @('', '.config', 'config', 'dotfiles\.config', 'dotfiles\config', 'dots\.config', 'home\.config', 'Configs\.config', 'Configs')
    foreach ($bRel in $bases) {
        $b = if ($bRel) { Join-Path $root $bRel } else { $root }
        foreach ($d in Dirs $b) {
            $leaf = Split-Path $d -Leaf
            $n = $leaf.ToLower()
            $rel = Rel $root $d
            if ($byName.ContainsKey($n)) {
                $t = $byName[$n]
                if (-not $seen[$t]) { & $add $t $rel $leaf; $seen[$t] = $true }
            } elseif ($n -eq 'quickshell') {
                foreach ($q in Dirs $d) { if (Exists (Join-Path $q 'shell.qml')) { & $add 'quickshell' (Rel $root $q) (Split-Path $q -Leaf) } }
                if (Exists (Join-Path $d 'shell.qml')) { & $add 'quickshell' $rel 'quickshell' }
            } elseif ($n -eq 'aether') {
                & $add 'aether' $rel 'aether'
            }
        }
        $st = Join-Path $b 'starship.toml'
        if ((Exists $st) -and -not $seen['starship']) { & $add 'starship' (Rel $root $st) 'starship.toml'; $seen['starship'] = $true }
    }
    $all = Files $root
    # hyprlock.conf outside a hypr part = a lock screen style (Hyprlock-Styles: Style-1..10, each its own folder)
    $sep = [IO.Path]::DirectorySeparatorChar
    $hyprPaths = @($parts | Where-Object { $_.type -eq 'hypr' } | ForEach-Object { $_.path + $sep })
    $confs = @($all | Where-Object { (Split-Path $_ -Leaf) -eq 'hyprlock.conf' } | Sort-Object { [regex]::Replace($_, '\d+', { param($m) $m.Value.PadLeft(6, '0') }) })
    foreach ($hc in $confs) {
        $rel = Rel $root (Split-Path $hc)
        if ($hyprPaths | Where-Object { ($rel + $sep).StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }) { continue }
        if (@($rel.Split($sep)).Count -gt 3) { continue }
        & $add 'hyprlock' $rel $(if ($rel) { Split-Path $rel -Leaf } else { '' })
    }
    $uc = $all | Where-Object { (Split-Path $_ -Leaf) -eq 'userChrome.css' } | Select-Object -First 1
    if ($uc) {
        & $add 'chrome' (Rel $root (Split-Path $uc)) 'chrome'
        # a theme in a folder named firefox is made for Firefox (Caelestia's hides the window buttons): kept out of Zen's own UI
        if ((Rel $root (Split-Path $uc)) -match '(^|\\)firefox(\\|$)') { $parts[$parts.Count - 1] | Add-Member -NotePropertyName app -NotePropertyValue 'firefox' }
    }
    $wt = $all | Where-Object { (Split-Path $_ -Leaf) -eq 'settings.json' -and (Read-Text $_).Contains('"schemes"') } | Select-Object -First 1
    if ($wt) { & $add 'wt' (Rel $root $wt) 'settings.json' }
    if ((Exists (Join-Path $root 'shell.qml')) -and -not ($parts | Where-Object { $_.type -eq 'quickshell' })) { & $add 'quickshell' '' '' }
    return , $parts.ToArray()
}

# ---------------------------------------------------------------- installing with backups
$script:files = New-Object System.Collections.ArrayList   # { target, backup } of everything this install touched
$script:newDirs = New-Object System.Collections.ArrayList # folders this install created (only these may go on uninstall)
$script:regs = New-Object System.Collections.ArrayList    # { key, name, old } registry values this install set
$fontKey = if ($env:AETHER_STORE_FONTKEY) { $env:AETHER_STORE_FONTKEY } else { 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Fonts' }   # tests use a scratch key
function Register-Font([string]$file) {   # per-user font: a value under HKCU ...\Fonts pointing at the file (Windows 10 1809+)
    $kind = if ([IO.Path]::GetExtension($file) -eq '.otf') { 'OpenType' } else { 'TrueType' }
    $name = "$([IO.Path]::GetFileNameWithoutExtension($file)) ($kind)"
    if (-not (Test-Path $fontKey)) { New-Item -Path $fontKey -Force | Out-Null }
    if (-not ($script:regs | Where-Object { $_.name -eq $name })) {
        $old = (Get-ItemProperty -Path $fontKey -Name $name -ErrorAction SilentlyContinue).$name
        [void]$script:regs.Add([pscustomobject]@{ key = $fontKey; name = $name; old = $old })
    }
    Set-ItemProperty -Path $fontKey -Name $name -Value $file
}
function Put-File([string]$src, [string]$target, $text = $null) {
    $d = [IO.Path]::GetDirectoryName($target)
    while ($d -and -not [IO.Directory]::Exists((LongPath $d))) {
        if (-not $script:newDirs.Contains($d)) { [void]$script:newDirs.Add($d) }
        $d = [IO.Path]::GetDirectoryName($d)
    }
    if (-not ($script:files | Where-Object { $_.target -eq $target })) {
        $b = $null
        if (Exists $target) { $b = Join-Path (Join-Path $backups "$script:id\files") ([guid]::NewGuid().ToString('N')); Copy-One $target $b }
        [void]$script:files.Add([pscustomobject]@{ target = $target; backup = $b })
    }
    if ($null -ne $text) { Write-Text $target $text } else { Copy-One $src $target }
}
function Put-Tree([string]$src, [string]$dest) {
    $n = 0
    foreach ($f in Files $src) { Put-File $f (Join-Path $dest (Rel $src $f)); $n++ }
    return $n
}

# terminal colours (kitty / foot / alacritty) -> @{ foreground, background, cursor, selection_background, color0..15 }
function Read-TermColours([string]$dir, [string]$type) {
    $c = @{}
    $names = @('black', 'red', 'green', 'yellow', 'blue', 'magenta', 'cyan', 'white')
    foreach ($f in Files $dir | Where-Object { [IO.Path]::GetExtension($_) -in '.conf', '.ini', '.toml', '' }) {
        $section = ''
        foreach ($line in ((Read-Text $f) -split "`r?`n")) {
            if ($line -match '^\s*\[([^\]]+)\]') { $section = $Matches[1].ToLower(); continue }
            if ($type -eq 'kitty' -and $line -match '^\s*(foreground|background|cursor|selection_background|color\d+)\s+(#[0-9a-fA-F]{6})') { $c[$Matches[1]] = $Matches[2] }
            elseif ($type -eq 'foot' -and $line -match '^\s*(foreground|background|regular\d|bright\d)\s*=\s*#?([0-9a-fA-F]{6})') {
                $k = $Matches[1]; $v = "#$($Matches[2])"
                if ($k -match '^regular(\d)$') { $c["color$($Matches[1])"] = $v } elseif ($k -match '^bright(\d)$') { $c["color$([int]$Matches[1] + 8)"] = $v } else { $c[$k] = $v }
            } elseif ($type -eq 'alacritty' -and $line -match '^\s*(\w+)\s*=\s*["''](?:#|0x)([0-9a-fA-F]{6})["'']') {
                $k = $Matches[1].ToLower(); $v = "#$($Matches[2])"
                if ($section -eq 'colors.primary' -and $k -in 'foreground', 'background') { $c[$k] = $v }
                elseif ($section -eq 'colors.cursor' -and $k -eq 'cursor') { $c['cursor'] = $v }
                elseif ($section -eq 'colors.normal' -and $names -contains $k) { $c["color$($names.IndexOf($k))"] = $v }
                elseif ($section -eq 'colors.bright' -and $names -contains $k) { $c["color$($names.IndexOf($k) + 8)"] = $v }
            }
        }
    }
    if (-not $c['foreground'] -or -not $c['background']) { return $null }
    return $c
}
function Strip-Jsonc([string]$s) {
    $sb = New-Object Text.StringBuilder; $i = 0; $inStr = $false
    while ($i -lt $s.Length) {
        $ch = $s[$i]; $nx = if ($i + 1 -lt $s.Length) { $s[$i + 1] } else { [char]0 }
        if ($inStr) { [void]$sb.Append($ch); if ($ch -eq [char]92) { [void]$sb.Append($nx); $i += 2; continue }; if ($ch -eq '"') { $inStr = $false }; $i++ }
        elseif ($ch -eq '"') { $inStr = $true; [void]$sb.Append($ch); $i++ }
        elseif ($ch -eq '/' -and $nx -eq '/') { while ($i -lt $s.Length -and $s[$i] -ne "`n") { $i++ } }
        elseif ($ch -eq '/' -and $nx -eq '*') { $i += 2; while ($i -lt $s.Length -and -not ($s[$i] -eq '*' -and $i + 1 -lt $s.Length -and $s[$i + 1] -eq '/')) { $i++ }; $i += 2 }
        else { [void]$sb.Append($ch); $i++ }
    }
    return ($sb.ToString() -replace ',(\s*[}\]])', '$1')
}
# Windows Terminal's settings.json is edited in place, not swapped for a copy: Aether writes its live "Aether" scheme
# there and you change it in Terminal, so uninstall takes out only what this install added (schemes by name - a scheme
# it replaced comes back - and profiles by guid) and keeps everything else as it is by then
$script:wt = [pscustomobject]@{ schemes = (New-Object Collections.ArrayList); replaced = (New-Object Collections.ArrayList); profiles = (New-Object Collections.ArrayList) }
function Edit-Wt($schemes, $wtProfile) {
    if (-not (Exists $wtSettings)) { Say "  Windows Terminal not found - skipped"; return }
    $j = Strip-Jsonc (Read-Text $wtSettings) | ConvertFrom-Json
    $schemes = @($schemes | Where-Object { $_ })
    if ($schemes.Count) {
        $names = @($schemes | ForEach-Object { $_.name })
        foreach ($s in $schemes) {
            if ($script:wt.schemes.Contains($s.name)) { continue }
            $old = @($j.schemes | Where-Object { $_.name -eq $s.name }) | Select-Object -First 1
            if ($old) { [void]$script:wt.replaced.Add($old) }
            [void]$script:wt.schemes.Add($s.name)
        }
        $j | Add-Member -NotePropertyName schemes -NotePropertyValue (@($j.schemes | Where-Object { $names -notcontains $_.name }) + $schemes) -Force
        Say "  Windows Terminal: colour scheme $($names -join ', ') added (pick it in Windows Terminal's settings)"
    }
    if ($wtProfile) {
        if (-not $j.profiles) { $j | Add-Member -NotePropertyName profiles -NotePropertyValue ([pscustomobject]@{ list = @() }) -Force }
        if ($j.profiles -is [array]) { $j.profiles = [pscustomobject]@{ defaults = [pscustomobject]@{}; list = $j.profiles } }
        $j.profiles | Add-Member -NotePropertyName list -NotePropertyValue (@($j.profiles.list | Where-Object { $_ -and $_.guid -ne $wtProfile.guid }) + @($wtProfile)) -Force
        [void]$script:wt.profiles.Add($wtProfile.guid)
    }
    Write-Text $wtSettings (($j | ConvertTo-Json -Depth 30) + "`n")
}
function Add-WtSchemes($schemes) { Edit-Wt $schemes $null }
function Undo-Wt($w) {
    if (-not $w -or -not (Exists $wtSettings)) { return }
    $names = @($w.schemes | Where-Object { $_ }); $guids = @($w.profiles | Where-Object { $_ })
    if (-not $names.Count -and -not $guids.Count) { return }
    $j = Strip-Jsonc (Read-Text $wtSettings) | ConvertFrom-Json
    if ($names.Count) { $j | Add-Member -NotePropertyName schemes -NotePropertyValue (@($j.schemes | Where-Object { $names -notcontains $_.name }) + @($w.replaced | Where-Object { $_ })) -Force }
    if ($guids.Count -and $j.profiles -and $j.profiles.list) {
        $j.profiles | Add-Member -NotePropertyName list -NotePropertyValue @($j.profiles.list | Where-Object { $guids -notcontains $_.guid }) -Force
        if ($guids -contains $j.defaultProfile) { $j.PSObject.Properties.Remove('defaultProfile') }
    }
    Write-Text $wtSettings (($j | ConvertTo-Json -Depth 30) + "`n")
    Say "  Windows Terminal: took out $($names.Count) scheme(s) and $($guids.Count) profile(s) this added"
}

# a Linux terminal's look (kitty / foot / alacritty: font, size, opacity, padding, cursor) -> a Windows Terminal profile
# "<rice> (<terminal>)" with that look and its colours - or, when the rice makes its colours from the wallpaper
# (matugen & co), Aether's "Aether" scheme, which Aether keeps in step with your wallpaper the same way
function Read-TermLook([string]$dir, [string]$type) {
    $l = @{}
    foreach ($f in Files $dir | Where-Object { [IO.Path]::GetExtension($_) -in '.conf', '.ini', '.toml', '' }) {
        $section = ''
        foreach ($line in ((Read-Text $f) -split "`r?`n")) {
            if ($line -match '^\s*\[([^\]]+)\]') { $section = $Matches[1].ToLower().Trim(); continue }
            if ($line -match '^\s*#') { continue }
            if ($type -eq 'kitty') {
                if ($line -match '^\s*font_family\s+(.+?)\s*$') {
                    $v = $Matches[1]; if ($v -match "family=(?:'([^']+)'|`"([^`"]+)`"|(\S+))") { $v = @($Matches[1], $Matches[2], $Matches[3]) | Where-Object { $_ } | Select-Object -First 1 }
                    $l.font = $v
                } elseif ($line -match '^\s*font_size\s+([\d.]+)') { $l.size = [double]$Matches[1] }
                elseif ($line -match '^\s*background_opacity\s+([\d.]+)') { $l.opacity = [double]$Matches[1] }
                elseif ($line -match '^\s*window_padding_width\s+([\d.]+)') { $l.pad = [int][double]$Matches[1] }
                elseif ($line -match '^\s*cursor_shape\s+(\w+)') { $l.cursor = $Matches[1].ToLower() }
            } elseif ($type -eq 'foot') {
                if ($section -in '', 'main') {
                    if ($line -match '^\s*font\s*=\s*([^:,]+)(?::[^,]*?size=([\d.]+))?') { $l.font = $Matches[1].Trim(); if ($Matches[2]) { $l.size = [double]$Matches[2] } }
                    elseif ($line -match '^\s*pad\s*=\s*(\d+)') { $l.pad = [int]$Matches[1] }
                } elseif ($section -like 'colors*' -and $line -match '^\s*alpha\s*=\s*([\d.]+)') { $l.opacity = [double]$Matches[1] }
                elseif ($section -eq 'cursor' -and $line -match '^\s*style\s*=\s*(\w+)') { $l.cursor = $Matches[1].ToLower() }
            } elseif ($type -eq 'alacritty') {
                if ($section -eq 'font.normal' -and $line -match '^\s*family\s*=\s*"([^"]+)"') { $l.font = $Matches[1] }
                elseif ($section -eq 'font' -and $line -match '^\s*size\s*=\s*([\d.]+)') { $l.size = [double]$Matches[1] }
                elseif ($section -eq 'window' -and $line -match '^\s*opacity\s*=\s*([\d.]+)') { $l.opacity = [double]$Matches[1] }
                elseif ($section -eq 'window.padding' -and $line -match '^\s*x\s*=\s*(\d+)') { $l.pad = [int]$Matches[1] }
                elseif ($section -eq 'cursor.style' -and $line -match '^\s*shape\s*=\s*"(\w+)"') { $l.cursor = $Matches[1].ToLower() }
            }
        }
    }
    return $l
}
function Font-Installed([string]$family) {
    try { Add-Type -AssemblyName System.Drawing; return [bool](@((New-Object Drawing.Text.InstalledFontCollection).Families | Where-Object { $_.Name -eq $family }).Count) } catch { return $true }
}
function Term-Profile($look, [string]$name, [string]$scheme) {
    $wtProfile = [ordered]@{ guid = '{' + [guid]::NewGuid().ToString() + '}'; name = $name; colorScheme = $scheme }
    # the shell you already use: the default profile's command line (a built-in profile has none - then PowerShell)
    $cmd = $null
    try {
        $j = Strip-Jsonc (Read-Text $wtSettings) | ConvertFrom-Json
        $def = @($j.profiles.list) + @($j.profiles) | Where-Object { $_ -and $_.guid -and $_.guid -eq $j.defaultProfile } | Select-Object -First 1
        if ($def.commandline) { $cmd = $def.commandline } elseif ([string]$def.source -like '*PowershellCore*' -and (Get-Command pwsh.exe -ErrorAction SilentlyContinue)) { $cmd = 'pwsh.exe' }
    } catch {}
    $wtProfile.commandline = if ($cmd) { $cmd } else { 'powershell.exe' }
    $font = [ordered]@{}
    if ($look.font -and $look.font -notin 'monospace', 'auto') {   # "X Bold" (kitty takes a style in the name) -> face X, weight bold
        $face = $look.font
        if ($face -match '^(.+?)\s+(Thin|ExtraLight|Light|Regular|Medium|SemiBold|Bold|ExtraBold|Black)$') { $face = $Matches[1]; if ($Matches[2] -ne 'Regular') { $font.weight = @{ Thin = 'thin'; ExtraLight = 'extra-light'; Light = 'light'; Medium = 'medium'; SemiBold = 'semi-bold'; Bold = 'bold'; ExtraBold = 'extra-bold'; Black = 'black' }[$Matches[2]] } }
        $font.face = $face
    }
    if ($look.size) { $font.size = $look.size }
    if ($font.Count) { $wtProfile.font = [pscustomobject]$font }
    if ($look.opacity -and $look.opacity -lt 1) { $wtProfile.opacity = [int][math]::Round($look.opacity * 100); $wtProfile.useAcrylic = $false }
    if ($look.pad) { $wtProfile.padding = "$($look.pad)" }
    $shape = @{ beam = 'bar'; bar = 'bar'; block = 'filledBox'; underline = 'underscore' }[[string]$look.cursor]
    if ($shape) { $wtProfile.cursorShape = $shape }
    return [pscustomobject]$wtProfile
}
function Term-Scheme($c, [string]$name) {
    $n = @('black', 'red', 'green', 'yellow', 'blue', 'purple', 'cyan', 'white')
    $s = [ordered]@{ name = $name; foreground = $c.foreground; background = $c.background }
    if ($c.cursor) { $s.cursorColor = $c.cursor }
    if ($c.selection_background) { $s.selectionBackground = $c.selection_background }
    for ($i = 0; $i -lt 16; $i++) {
        $v = $c["color$i"]; if (-not $v) { continue }
        $k = if ($i -lt 8) { $n[$i] } else { 'bright' + $n[$i - 8].Substring(0, 1).ToUpper() + $n[$i - 8].Substring(1) }
        $s[$k] = $v
    }
    return [pscustomobject]$s
}

function Install-Part($p, [string]$src, [string]$rice) {
    $from = if ($p.path) { Join-Path $src $p.path } else { $src }
    switch ($p.type) {
        'hypr' {
            # AetherWM reads hyprland.conf; the programs a Linux rice starts are Linux ones -> commented out, keys kept
            foreach ($f in Files $from) {
                $t = Join-Path (Join-Path $cfg 'hypr') (Rel $from $f)
                $ext = [IO.Path]::GetExtension($f)
                if ($ext -eq '.conf') {
                    Put-File $null $t ((Read-Text $f) -replace '(?m)^(\s*)(exec(-once)?\s*=)', '$1# (Linux program - Aether) $2')
                } elseif ($ext -eq '.lua') {   # hyprland.lua (Hyprland's newer config): start-up programs are hl.exec_cmd(...)
                    Put-File $null $t ((Read-Text $f) -replace '(?m)^(\s*)(hl\.exec_cmd\()', '$1-- (Linux program - Aether) $2')
                } else { Put-File $f $t }
            }
            Say "  AetherWM: ~/.config/hypr (programs it starts are Linux ones: commented out; keys, looks and rules kept)"
        }
        'caelestia' { $n = Put-Tree $from (Join-Path $cfg 'caelestia'); Say "  Caelestia settings: $n files -> ~/.config/caelestia" }
        'noctalia' { $n = Put-Tree $from (Join-Path $cfg 'noctalia'); Say "  Noctalia settings: $n files -> ~/.config/noctalia" }
        'ryoku' { $n = Put-Tree $from (Join-Path $cfg 'ryoku'); Say "  Ryoku settings: $n files -> ~/.config/ryoku" }
        'dms' { $n = Put-Tree $from (Join-Path $env:LOCALAPPDATA 'quickshell\dms'); Say "  DMS settings: $n files" }
        'end4' { $n = Put-Tree $from (Join-Path $env:LOCALAPPDATA 'quickshell\illogical-impulse'); Say "  end-4 settings: $n files" }
        'quickshell' {
            $name = if ($p.name -and $p.name -ne 'quickshell') { "$rice-$($p.name)" } else { $rice }
            $dest = Join-Path $cfg "quickshell\$name"
            if ([IO.Directory]::Exists((LongPath $dest))) { Say "  ~/.config/quickshell/$name already exists - skipped"; return }
            $n = Put-Tree $from $dest
            Say "  Quickshell config: $n files -> ~/.config/quickshell/$name (made for Linux: may need porting to run)"
        }
        { $_ -in 'kitty', 'foot', 'alacritty' } {
            $c = Read-TermColours $from $p.type
            $look = Read-TermLook $from $p.type
            $title = "$rice ($($p.type))"
            if (Exists $wtSettings) {
                $scheme = if ($c) { Term-Scheme $c $title } else { $null }
                Edit-Wt @($scheme) (Term-Profile $look $title $(if ($c) { $title } else { 'Aether' }))
                $bits = @(); if ($look.font) { $bits += "$($look.font)$(if ($look.size) { " $($look.size)" })" } elseif ($look.size) { $bits += "size $($look.size)" }
                if ($look.opacity -and $look.opacity -lt 1) { $bits += "$([int][math]::Round($look.opacity * 100))% opacity" }
                if ($look.pad) { $bits += "padding $($look.pad)" }
                $colours = if ($c) { 'its colours' } else { "Aether's wallpaper colours (the rice makes its own from the wallpaper)" }
                Say "  Windows Terminal: profile `"$title`" ($((@($bits) + $colours) -join ', ')) - open it from Terminal's new-tab menu"
                $face = ([string]$look.font -replace '\s+(Thin|ExtraLight|Light|Regular|Medium|SemiBold|Bold|ExtraBold|Black)$', '')
                if ($look.font -and $look.font -notin 'monospace', 'auto' -and -not (Font-Installed $face)) { Say "  (font `"$face`" isn't installed - Terminal uses its own until you install it)" }
            } else { Say "  Windows Terminal not found - $title skipped" }
            if ($p.type -eq 'alacritty') { $n = Put-Tree $from (Join-Path $env:APPDATA 'alacritty'); Say "  Alacritty: $n files -> %APPDATA%\alacritty" }
        }
        'wezterm' { $n = Put-Tree $from (Join-Path $cfg 'wezterm'); Say "  WezTerm: $n files -> ~/.config/wezterm" }
        'fastfetch' {
            $dest = Join-Path $cfg 'fastfetch'
            if ($p.into) { $dest = Join-Path $dest $p.into }
            $n = Put-Tree $from $dest; Say "  fastfetch: $n files -> ~/.config/fastfetch$(if ($p.into) { "/$($p.into)" }) (fastfetch --config <name> picks one)"
        }
        'starship' { Put-File $from (Join-Path $cfg 'starship.toml'); Say "  Starship: ~/.config/starship.toml" }
        'cava' { $n = Put-Tree $from (Join-Path $cfg 'cava'); Say "  cava: $n files -> ~/.config/cava" }
        'waybar' { $n = Put-Tree $from (Join-Path $cfg 'waybar'); Say "  Waybar: $n files -> ~/.config/waybar" }
        'fish' { $n = Put-Tree $from (Join-Path $cfg 'fish'); Say "  fish: $n files -> ~/.config/fish" }
        'wallpapers' {
            $paths = Read-Json (Join-Path $cfg 'caelestia\paths.json')
            $walls = if ($paths -and $paths.wallpaperDir) { ($paths.wallpaperDir -replace '^~', $home_) -replace '/', '\' } else { Join-Path $home_ 'Pictures\Wallpapers' }
            $n = Put-Tree $from (Join-Path $walls $rice); Say "  Wallpapers: $n -> $walls\$rice"
        }
        'chrome' {
            # app: "firefox" / "zen" = only that browser (Firefox themes break Zen's own UI); only: [paths] = just these
            # files/folders of path; into: a sub-folder of chrome/ plus userChrome.css / userContent.css that @import it
            # (firefox-gnome-theme's way); userjs: a user.js in the repo whose prefs go into the profile's user.js
            $profiles = @(Browser-Profiles | Where-Object { -not $p.app -or $_.app -eq $p.app -or ($p.app -eq 'firefox' -and $_.app -eq 'Firefox') })
            $what = if ($p.app) { $p.app } else { 'Zen / Firefox' }
            if (-not $profiles) { Say "  no $what profile found - browser theme skipped" }
            foreach ($z in $profiles) {
                $chrome = Join-Path $z.dir 'chrome'
                $dest = if ($p.into) { Join-Path $chrome $p.into } else { $chrome }
                $n = 0
                if ($p.only) {
                    foreach ($o in @($p.only)) {
                        $s = Join-Path $from ($o -replace '/', '\')
                        if (Exists $s) { Put-File $s (Join-Path $dest ($o -replace '/', '\')); $n++ } else { $n += Put-Tree $s (Join-Path $dest ($o -replace '/', '\')) }
                    }
                } else { $n = Put-Tree $from $dest }
                if ($p.into) {
                    foreach ($css in 'userChrome.css', 'userContent.css') {
                        if (-not (Exists (Join-Path $dest $css))) { continue }
                        $t = Join-Path $chrome $css
                        $line = "@import `"$($p.into)/$css`";"
                        $old = if (Exists $t) { Read-Text $t } else { '' }
                        if (-not $old.Contains($line)) { Put-File $null $t ("$line`n" + $old) }   # @imports must come first
                    }
                }
                # Firefox reads userChrome.css only with this pref (Zen has it on); plus the theme's own prefs
                $prefs = @('user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);')
                if ($p.userjs -and (Exists (Join-Path $src ($p.userjs -replace '/', '\')))) {
                    $prefs += @((Read-Text (Join-Path $src ($p.userjs -replace '/', '\'))) -split "`r?`n" | Where-Object { $_ -match '^\s*user_pref\(' } | ForEach-Object { $_.Trim() })
                }
                $uj = Join-Path $z.dir 'user.js'
                $old = if (Exists $uj) { Read-Text $uj } else { '' }
                $add = @($prefs | Where-Object { -not $old.Contains($_) })
                if ($add.Count) { Put-File $null $uj ($old.TrimEnd() + "`n// AetherStore: $rice`n" + ($add -join "`n") + "`n").TrimStart() }
                Say "  $($z.app): $n theme files in its profile$(if ($add.Count) { ", $($add.Count) settings in user.js" }) (restart it)"
            }
        }
        'wt' { $j = Strip-Jsonc (Read-Text $from) | ConvertFrom-Json; if ($j.schemes) { Add-WtSchemes @($j.schemes) } }
        'wtscheme' {   # loose scheme files (theme repos): a .json file, or every scheme .json in a folder
            $list = if (Exists $from) { @($from) } else { @(Files $from | Where-Object { [IO.Path]::GetExtension($_) -eq '.json' }) }
            $schemes = @()
            foreach ($f in $list) {
                try { $o = Strip-Jsonc (Read-Text $f) | ConvertFrom-Json } catch { continue }
                foreach ($s in @($o)) { if ($s.name -and $s.background -and $s.foreground) { $schemes += $s } }   # themes (tab/window) are not schemes
            }
            if ($schemes.Count) { Add-WtSchemes $schemes } else { Say "  no colour schemes in $($p.path)" }
        }
        'aether' { $n = Put-Tree $from (Join-Path $cfg 'caelestia\user'); Say "  Aether pieces: $n files -> ~/.config/caelestia/user" }
        'hyprlock' {
            # styles point at ~/.config/hypr/<file> (images, Scripts/), so the style folder goes there as it is
            $dest = Join-Path $cfg 'hypr'
            $n = 0; $fonts = 0
            foreach ($f in Files $from) {
                $t = Join-Path $dest (Rel $from $f)
                $isFont = [IO.Path]::GetExtension($f) -in '.ttf', '.otf'
                # a font another style already put there may be in use: same size = same font, leave it
                $same = $isFont -and (Exists $t) -and ((New-Object IO.FileInfo (LongPath $t)).Length -eq (New-Object IO.FileInfo (LongPath $f)).Length)
                if (-not $same) { Put-File $f $t; $n++ }
                if ($isFont) { Register-Font $t; $fonts++ }
            }
            $fontNote = if ($fonts) { ", $fonts fonts installed for you" } else { "" }
            Say "  Hyprlock: $n files -> ~/.config/hypr$fontNote. Use it: Aether Settings > Lock screen > Hyprlock"
        }
        default { Say "  (don't know how to install '$($p.type)' - skipped)" }
    }
}

# ---------------------------------------------------------------- actions
$installed = Read-Json $installedFile
if (-not $installed) { $installed = [pscustomobject]@{ rices = @() } }

try {
    switch ($Action) {
        'scan' {
            $Url = $Url.Trim()
            $Url = $Url.Trim('"')
            if ($Url -notmatch '^https?://' -and -not (Test-Path -LiteralPath $Url)) { Fail "That is not a web link (https://...) or a folder / .zip on this PC" }
            $name = ($Url -replace '[?#].*$', '' -replace '/+$', '' -replace '\.git$', '' -replace '\.zip$', '').Split('/')[-1]
            $owner = ''
            if ($Url -match 'github\.com/([^/]+)/([^/#?]+)') { $owner = $Matches[1]; $name = $Matches[2] -replace '\.git$', '' }
            if ($Url -notmatch '^https?://') { $name = [IO.Path]::GetFileName($Url.TrimEnd('\', '/')) -replace '\.zip$', '' }
            $name = ($name -replace '[^\w.-]', '-').Trim('.-')
            $id = ($(if ($owner) { "$owner-$name" } else { $name }) -replace '[^\w.-]', '-').ToLower()   # many theme repos share a name
            $dir = Join-Path $cache $id
            Remove-Tree $dir
            $root = Fetch $Url $dir
            $manifest = Read-Json (Join-Path $root 'aether-store.json')
            if ($Hint) {   # from the browse list: base64 JSON (a command line eats JSON's quotes)
                # PS 5.1 hands a JSON list back as ONE object: piping it again unrolls it
                $manifest = [pscustomobject]@{ parts = @(([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Hint)) | ConvertFrom-Json) | ForEach-Object { $_ }) }
            }
            $found = @(if ($manifest -and $manifest.parts) { $manifest.parts | ForEach-Object { [pscustomobject]@{ type = $_.type; path = ([string]$_.path -replace '/', '\'); name = $_.name; into = $_.into; app = $_.app; only = $_.only; userjs = $_.userjs; label = $labels[$_.type] } } } else { Find-Parts $root })
            $found = @($found | ForEach-Object { $_ })   # Find-Parts hands back its list as one object
            foreach ($p in $found) {   # several of a type (hyprlock styles): each its own key
                $same = @($found | Where-Object { $_.type -eq $p.type }).Count
                $key = if ($same -gt 1 -and $p.name) { "$($p.type):$($p.name)" } else { $p.type }
                $p | Add-Member -NotePropertyName key -NotePropertyValue $key -Force
                if ($p.type -eq 'hyprlock' -and $p.name) { $p.label = "Hyprlock lock screen: $($p.name)" }
            }
            if ($manifest -and $manifest.name) { $name = $manifest.name }
            if ($Name) { $name = $Name }
            $info = [pscustomobject]@{ id = $id; name = $name; url = $Url; root = $root; parts = @($found) }
            Write-Json (Join-Path $dir '.aether-scan.json') $info
            if (-not $found.Count) { Fail "Nothing in it that Aether knows how to install" }
            Say ("PARTS " + ($info | ConvertTo-Json -Depth 6 -Compress))
        }
        'install' {
            $script:id = $Id
            $info = Read-Json (Join-Path (Join-Path $cache $Id) '.aether-scan.json')
            if (-not $info) { Fail "Scan it first" }
            if ($installed.rices | Where-Object { $_.id -eq $Id }) { Fail "$($info.name) is already installed - uninstall it first" }
            $want = @(if ($Parts) { $Parts.Split(',') | ForEach-Object { $_.Trim() } } else { $info.parts | ForEach-Object { $_.type } })
            try {
                foreach ($p in $info.parts) {
                    if ($want -notcontains $p.type -and -not ($p.key -and $want -contains $p.key)) { continue }
                    Say "$($p.label):"
                    Install-Part $p $info.root $info.name
                }
            } finally {   # even a half-done install is recorded, so uninstall can undo it
                $entry = [pscustomobject]@{ id = $Id; name = $info.name; url = $info.url; parts = $want; installed = (Get-Date).ToString('s'); files = $script:files.ToArray(); dirs = $script:newDirs.ToArray(); regs = $script:regs.ToArray(); wt = [pscustomobject]@{ schemes = $script:wt.schemes.ToArray(); replaced = $script:wt.replaced.ToArray(); profiles = $script:wt.profiles.ToArray() } }
                $installed.rices = @($installed.rices) + @($entry)
                Write-Json $installedFile $installed
            }
            Say "OK $Id"
        }
        'uninstall' {
            $r = $installed.rices | Where-Object { $_.id -eq $Id } | Select-Object -First 1
            if (-not $r) { Fail "$Id is not installed" }
            foreach ($g in @($r.regs | Where-Object { $_ })) {   # fonts registered for you: the old value back, or gone
                if ($null -ne $g.old) { Set-ItemProperty -Path $g.key -Name $g.name -Value $g.old }
                else { Remove-ItemProperty -Path $g.key -Name $g.name -ErrorAction SilentlyContinue }
            }
            Undo-Wt $r.wt
            foreach ($f in @($r.files)) {
                if ($f.backup -and (Exists $f.backup)) { Copy-One $f.backup $f.target }
                elseif (-not $f.backup -and (Exists $f.target)) { [IO.File]::Delete((LongPath $f.target)) }
            }
            # folders the install created, deepest first, when they are empty again (folders that were there stay)
            foreach ($d in @($r.dirs | Where-Object { $_ } | Sort-Object { $_.Length } -Descending)) {
                if ([IO.Directory]::Exists((LongPath $d)) -and -not [IO.Directory]::EnumerateFileSystemEntries((LongPath $d)).GetEnumerator().MoveNext()) {
                    [IO.Directory]::Delete((LongPath $d))
                }
            }
            $installed.rices = @($installed.rices | Where-Object { $_.id -ne $Id })
            Write-Json $installedFile $installed
            Remove-Tree (Join-Path $backups $Id)
            Say "Put back $(@($r.files).Count) files"
            Say "OK $Id"
        }
        'list' { Say ("INSTALLED " + (@($installed.rices | ForEach-Object { [pscustomobject]@{ id = $_.id; name = $_.name; url = $_.url; parts = @($_.parts); installed = $_.installed } }) | ConvertTo-Json -Depth 5 -Compress)) }
        default { Fail "scan | install | uninstall | list" }
    }
} catch {
    Fail "$($_.Exception.Message) (line $($_.InvocationInfo.ScriptLineNumber))"
}
