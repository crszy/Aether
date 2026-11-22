@echo off
rem Builds hyprlua.exe: Lua 5.5.1 (ref\lua\lua-5.5.1\src) + hyprlua.c. Console program.
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
cd /d "%~dp0"
set LUA=%~dp0..\..\ref\lua\lua-5.5.1\src
if not exist out mkdir out
set SRC=
for %%f in ("%LUA%\*.c") do (
  if /i not "%%~nxf"=="lua.c" if /i not "%%~nxf"=="luac.c" if /i not "%%~nxf"=="onelua.c" call set SRC=%%SRC%% "%%f"
)
cl /nologo /O2 /MT /DLUA_COMPAT_5_4 /I"%LUA%" hyprlua.c %SRC% /Fo"out\\" /Fe"hyprlua.exe" /link /SUBSYSTEM:CONSOLE >out\build.log || (type out\build.log & exit /b 1)
echo OK
