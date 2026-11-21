# Copies Aether's examples (modules/userpanel/template) into your user folder (~/.config/caelestia/user) and keeps
# them up to date without touching your work. Run by services/UserExt.qml when Aether brings new examples.
#   seed-examples.ps1 -From <template folder> -To <user folder>
# <user>\.aether\examples holds each example exactly as it was delivered. For every example file:
#   missing in your folder              -> copied in
#   same as the delivered copy          -> you never changed it: replaced by the new version
#   changed by you                      -> left alone
#   README.txt                          -> always the current one (it is Aether's help, not yours)
param([string]$From, [string]$To)
$ErrorActionPreference = 'Stop'
$base = Join-Path $To '.aether\examples'
function Hash([string]$p) { if (Test-Path -LiteralPath $p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash } else { '' } }
function Put([string]$src, [string]$dst) {
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    Copy-Item -LiteralPath $src -Destination $dst -Force
}
$fromFull = (Get-Item -LiteralPath $From).FullName.TrimEnd('\')   # same spelling as its files' FullName (8.3 names expanded)
$added = 0; $updated = 0; $kept = 0
foreach ($f in Get-ChildItem -LiteralPath $fromFull -Recurse -File) {
    $rel = $f.FullName.Substring($fromFull.Length + 1)
    $dst = Join-Path $To $rel
    $b = Join-Path $base $rel
    $new = Hash $f.FullName
    if ($rel -eq 'README.txt') { Put $f.FullName $dst; continue }
    $mine = Hash $dst
    $was = Hash $b
    if (-not $mine) { Put $f.FullName $dst; Put $f.FullName $b; $added++ }
    elseif ($mine -eq $new) { if ($was -ne $new) { Put $f.FullName $b } }   # already current: remember it
    elseif ($was -and $mine -eq $was) { Put $f.FullName $dst; Put $f.FullName $b; $updated++ }
    else { $kept++ }
}
Write-Output "OK added $added, updated $updated, kept $kept you changed"
