GNU assembler for building AetherWM without Visual Studio.

Rust's GNU toolchain (rustup) ships dlltool.exe but not the assembler (as.exe) that dlltool runs to make import
libraries, so the build stops with "dlltool: CreateProcess". build-aether-tiling.ps1 puts rustup's dlltool.exe
next to this as.exe and points Rust at it (-C dlltool=...). Nothing here is installed system-wide.

From WinLibs (MinGW-w64 GCC 15 + binutils 2.47, UCRT build, by Brecht Sanders): https://winlibs.com
Source code: https://github.com/brechtsanders/winlibs_mingw (build recipes) and the upstream projects:
  as.exe               GNU binutils 2.47        GPL-3.0      https://sourceware.org/binutils/
  libintl-8.dll        GNU gettext              LGPL-2.1+    https://www.gnu.org/software/gettext/
  libiconv-2.dll       GNU libiconv             LGPL-2.1+    https://www.gnu.org/software/libiconv/
  zlib1.dll            zlib                     zlib         https://zlib.net
  libzstd.dll          Zstandard                BSD-3        https://github.com/facebook/zstd
  libwinpthread-1.dll  MinGW-w64 winpthreads    MIT / ZPL    https://www.mingw-w64.org
