@echo off
REM ============================================================
REM  Emergency recovery: put explorer.exe back as the Windows shell.
REM
REM  If you sign in and there is no desktop / no taskbar:
REM     Ctrl + Shift + Esc  ->  File  ->  Run new task  ->  browse to this file
REM  or type its full path:
REM     Z:\CaelestiaWin\RESTORE-EXPLORER.bat
REM ============================================================

echo Removing the per-user shell override...
reg delete "HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon" /v Shell /f 2>nul

echo Starting explorer.exe now...
start "" explorer.exe

echo.
echo Done. Explorer is the shell again and will start normally at your next sign-in.
echo You can close this window.
pause
