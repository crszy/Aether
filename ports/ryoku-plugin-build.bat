@echo off
rem Builds Ryoku.Blobs for Windows (sources made by ryoku-port.js) and puts the module into ryoku-win\Ryoku\Blobs
setlocal
set SRC=%~dp0ryoku-plugin-win
set OUT=%~dp0ryoku-plugin-build
set LLVM=C:/Program Files/LLVM/bin
set QT=C:\Qt\6.11.2\msvc2022_64
set VSC=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
set PATH=C:\Program Files\LLVM\bin;%VSC%\CMake\bin;%VSC%\Ninja;%QT%\bin;%PATH%
cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="%LLVM%/clang-cl.exe" -DCMAKE_CXX_COMPILER="%LLVM%/clang-cl.exe" -DCMAKE_LINKER="%LLVM%/lld-link.exe" -DCMAKE_PREFIX_PATH="%QT%" -DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHsc /GR" || exit /b 1
cmake --build "%OUT%" || exit /b 1
if not exist "%~dp0ryoku-win\Ryoku\Blobs" mkdir "%~dp0ryoku-win\Ryoku\Blobs"
xcopy /E /Y /Q "%OUT%\qml\Ryoku\Blobs\*" "%~dp0ryoku-win\Ryoku\Blobs\" >nul
echo RC=0
