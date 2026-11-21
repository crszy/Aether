@echo off
rem Builds Caelestia.exe (the launcher, with the Caelestia logo as its icon) and caelestia.exe (the CLI shim).
setlocal
set QT=C:\Qt\6.11.2\msvc2022_64
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
if not exist out\cli mkdir out\cli

rem 1. icon from Caelestia's logo
cl /nologo /O2 /EHsc /std:c++17 /Zc:__cplusplus /permissive- /MD /I"%QT%\include" /I"%QT%\include\QtCore" /I"%QT%\include\QtGui" /I"%QT%\include\QtSvg" ^
   mkicon.cpp /Fo"out\\" /Fe"out\mkicon.exe" /link "%QT%\lib\Qt6Core.lib" "%QT%\lib\Qt6Gui.lib" "%QT%\lib\Qt6Svg.lib" >out\mkicon.log || (type out\mkicon.log & exit /b 1)
set PATH=%QT%\bin;%PATH%
set QT_PLUGIN_PATH=%QT%\plugins
set QT_QPA_PLATFORM=offscreen
out\mkicon.exe "%~dp0..\caelestia-shell\assets\logo.svg" out\caelestia.ico "#1e1e2e" || exit /b 1

rem 2. Caelestia.exe (launcher) with the icon
> out\launcher.rc echo 1 ICON "caelestia.ico"
rc /nologo /fo out\launcher.res out\launcher.rc || exit /b 1
cl /nologo /O2 /EHsc /std:c++17 /DUNICODE /D_UNICODE CaelestiaShell.cpp out\launcher.res /Fo"out\\" /Fe"out\Caelestia.exe" ^
   /link /SUBSYSTEM:WINDOWS shell32.lib user32.lib advapi32.lib >out\launcher.log || (type out\launcher.log & exit /b 1)

rem 3. caelestia.exe (CLI shim used by the shell's QML)
cl /nologo /O2 /EHsc /std:c++17 /DUNICODE /D_UNICODE caelestia.cpp /Fo"out\cli\\" /Fe"out\cli\caelestia.exe" ^
   /link /SUBSYSTEM:WINDOWS shell32.lib user32.lib >out\cli.log || (type out\cli.log & exit /b 1)
copy /y out\cli\caelestia.exe caelestia.exe >nul

rem 4. hyprctl.exe (Hyprland's hyprctl, talks to the shell's HyprConf service)
cl /nologo /O2 /EHsc /std:c++17 /DUNICODE /D_UNICODE hyprctl.cpp /Fo"out\cli\\" /Fe"out\cli\hyprctl.exe" ^
   /link /SUBSYSTEM:CONSOLE >out\hyprctl.log || (type out\hyprctl.log & exit /b 1)
copy /y out\cli\hyprctl.exe hyprctl.exe >nul
echo OK
