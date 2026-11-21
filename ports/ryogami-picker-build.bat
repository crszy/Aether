@echo off
rem Builds Ryogami's picker (Ryoku.Ryogami) for Windows from ryogami-picker-win (ryoku-port.js makes it) into
rem ryogami-picker-build\qml\Ryoku\Ryogami, then copies the module into ryoku-win\Ryoku\Ryogami
setlocal
set SRC=%~dp0ryogami-picker-win\picker
set OUT=%~dp0ryogami-picker-build
set LLVM=C:/Program Files/LLVM/bin
set QT=C:\Qt\6.11.2\msvc2022_64
set VSC=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
set PATH=C:\Program Files\LLVM\bin;%VSC%\CMake\bin;%VSC%\Ninja;%QT%\bin;%PATH%
cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="%LLVM%/clang-cl.exe" -DCMAKE_CXX_COMPILER="%LLVM%/clang-cl.exe" -DCMAKE_LINKER="%LLVM%/lld-link.exe" -DCMAKE_PREFIX_PATH="%QT%" -DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHsc /GR" || exit /b 1
cmake --build "%OUT%" || exit /b 1
if not exist "%~dp0ryoku-win\Ryoku\Ryogami" mkdir "%~dp0ryoku-win\Ryoku\Ryogami"
xcopy /E /Y /Q "%OUT%\qml\Ryoku\Ryogami\*" "%~dp0ryoku-win\Ryoku\Ryogami\" >nul
echo RC=0
