@echo off
rem Builds the Caelestia C++ QML plugin for Windows (run make-caelestia-win.py first)
setlocal
set SRC=%~dp0caelestia-shell-win
set OUT=%SRC%\build
set LLVM=C:/Program Files/LLVM/bin
set QT=C:\Qt\6.11.2\msvc2022_64
set VSC=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
set PATH=C:\Program Files\LLVM\bin;%VSC%\CMake\bin;%VSC%\Ninja;%QT%\bin;%PATH%
if not exist "%OUT%\build.ninja" (
  cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_C_COMPILER="%LLVM%/clang-cl.exe" -DCMAKE_CXX_COMPILER="%LLVM%/clang-cl.exe" -DCMAKE_LINKER="%LLVM%/lld-link.exe" ^
    -DCMAKE_PREFIX_PATH="%QT%" ^
    -DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHsc /GR /D_USE_MATH_DEFINES /DNOMINMAX /FIranges /FIC:/Users/Administrator/Desktop/CaelestiaWin/dotfiles/caelestia-winexport.hpp" ^
    -DVERSION=1.0.0 -DGIT_REVISION=bcbb5d5 -DDISTRIBUTOR=aether-windows ^
    -DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON -DENABLE_MODULES=plugin %* || exit /b 1
)
cmake --build "%OUT%" -- -k 0 || exit /b 1
echo OK
