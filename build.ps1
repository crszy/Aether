# Build Aether.exe — single cl.exe call (MSVC 2022).
$ErrorActionPreference = "Stop"
# Never kill the shell: when Aether IS the shell, it may be relaunched instantly and the exe stays
# locked. Rename the running image aside instead - Windows allows that - and link fresh.
if (Test-Path "$PSScriptRoot\Aether.exe") {
  try { [IO.File]::OpenWrite("$PSScriptRoot\Aether.exe").Close() }
  catch {
    Remove-Item "$PSScriptRoot\Aether.old.exe" -Force -EA SilentlyContinue
    Rename-Item "$PSScriptRoot\Aether.exe" "Aether.old.exe" -EA SilentlyContinue
    Write-Host "  (shell is live - renamed the running exe aside)" -ForegroundColor DarkGray
  }
}

$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
# the Settings search index (every control label on every page) is generated from main.cpp
if (Get-Command python -EA SilentlyContinue) { python "$PSScriptRoot\tools\gen_settings_index.py" | Out-Null }
$dir    = $PSScriptRoot                     # wherever the repo was cloned

# Lua 5.4 (plugin engine) is built ONCE into lua54.lib — it is C, so it cannot ride in the
# /std:c++17 compile of the shell. Delete lua54.lib to force a rebuild.
if (-not (Test-Path "$PSScriptRoot\lua54.lib")) {
  Write-Host "Building lua54.lib..." -ForegroundColor Cyan
  $luaSrc = (Get-ChildItem "$PSScriptRoot\lua\*.c" | Where-Object { $_.Name -notin @("lua.c","luac.c","onelua.c") } |
             ForEach-Object { "lua\" + $_.Name }) -join " "
  $lcmd = "`"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && " +
          "cd /d `"$PSScriptRoot`" && cl /nologo /c /O2 /MT /DWIN32 /D_CRT_SECURE_NO_WARNINGS /Ilua /Foluaobj\ $luaSrc && " +
          "lib /nologo /OUT:lua54.lib luaobj\*.obj"
  New-Item -ItemType Directory -Force "$PSScriptRoot\luaobj" | Out-Null
  $lout = cmd /c $lcmd 2>&1
  if (-not (Test-Path "$PSScriptRoot\lua54.lib")) {
    Write-Host "LUA BUILD FAILED:" -ForegroundColor Red; $lout | Select-Object -Last 20 | ForEach-Object { Write-Host $_ }; exit 1 }
  Write-Host "  OK -> lua54.lib" -ForegroundColor DarkGray
}

$srcs = @(
  "main.cpp",
  "imgui\imgui.cpp","imgui\imgui_draw.cpp","imgui\imgui_tables.cpp","imgui\imgui_widgets.cpp",
  "imgui\backends\imgui_impl_dx11.cpp","imgui\backends\imgui_impl_win32.cpp"
) -join " "

$libs = "d3d11.lib dxgi.lib dwmapi.lib pdh.lib iphlpapi.lib advapi32.lib d3dcompiler.lib user32.lib gdi32.lib shell32.lib ole32.lib comdlg32.lib rasapi32.lib lua54.lib"

$cmd = "`"$vcvars`" >nul 2>&1 && cd /d `"$dir`" && " +
       "cl /nologo /EHsc /std:c++17 /O2 /MT /bigobj /DUNICODE /D_UNICODE /DWIN32 /DNOMINMAX " +
       "/I. /Iimgui /Iimgui\backends /Ilua /FeAether.exe $srcs /link /SUBSYSTEM:WINDOWS /MAP:Aether.map $libs"

Write-Host "Building Aether..." -ForegroundColor Cyan
$out = cmd /c $cmd 2>&1
$errs = $out | Select-String -Pattern "error [A-Z]|LNK[0-9]|fatal" | Select-Object -First 25
if ($errs) { Write-Host "BUILD FAILED:" -ForegroundColor Red; $errs | ForEach-Object { Write-Host $_ }; exit 1 }
if (Test-Path "$dir\Aether.exe") {
    Write-Host "OK -> $dir\Aether.exe" -ForegroundColor Green
    # keep the MSIX package copy current (packaged launch runs from pkg\ -> gives package identity for notifications)
    if (Test-Path "$dir\pkg") { Copy-Item "$dir\Aether.exe" "$dir\pkg\Aether.exe" -Force; Write-Host "  (updated pkg\Aether.exe)" -ForegroundColor DarkGray }
} else { Write-Host "No exe. Full output:" -ForegroundColor Red; $out | ForEach-Object { Write-Host $_ }; exit 1 }
