#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/env.sh"

need() {
	command -v "$1" >/dev/null || { echo "missing $1: $2" >&2; exit 1; }
}
need clang-cl "install clang"
need llvm-rc "install llvm"
need cmake "install cmake"
need ninja "install ninja"
need python3 "install python3"
need git "install git"
need curl "install curl"
need xwin "cargo install xwin --locked"
need aqt "pipx install git+https://github.com/miurahr/aqtinstall (3.3.0 can't read the Qt 6.11 repository)"

mkdir -p "$QSW_TOOLCHAIN/bin"

if command -v lld-link >/dev/null; then
	ln -sfn "$(command -v lld-link)" "$QSW_TOOLCHAIN/bin/lld-link"
elif command -v rustc >/dev/null && [ -x "$(rustc --print sysroot)/lib/rustlib/x86_64-unknown-linux-gnu/bin/rust-lld" ]; then
	ln -sfn "$(rustc --print sysroot)/lib/rustlib/x86_64-unknown-linux-gnu/bin/rust-lld" "$QSW_TOOLCHAIN/bin/lld-link"
else
	echo "missing lld-link: install lld (or rustup, whose rust-lld works too)" >&2
	exit 1
fi
ln -sfn "$(command -v llvm-rc)" "$QSW_TOOLCHAIN/bin/rc"

modules=(qt5compat qtimageformats qtpositioning qtshadertools qtsvg)
if [ ! -d "$QT_HOST" ]; then
	aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 -m "${modules[@]}" -O "$QSW_TOOLCHAIN/qt"
fi
if [ ! -d "$QT_WIN" ]; then
	aqt install-qt windows desktop "$QT_VERSION" win64_msvc2022_64 -m "${modules[@]}" -O "$QSW_TOOLCHAIN/qt"
fi

if [ ! -d "$XWIN/crt" ]; then
	xwin --cache-dir "$QSW_TOOLCHAIN/xwin-cache" --sdk-version 10.0.26100 splat --output "$XWIN"
	python3 "$QSW/windows/fix-xwin-case.py" "$XWIN"
fi

if [ ! -d "$QSW_TOOLCHAIN/cli11" ]; then
	git clone -q --depth 1 --branch v2.5.0 https://github.com/CLIUtils/CLI11.git "$QSW_TOOLCHAIN/cli11"
fi

fetch() {
	local url="$1" out="$2" sum="$3"
	if [ -f "$out" ] && echo "$sum  $out" | sha256sum -c --status; then return; fi
	mkdir -p "$(dirname "$out")"
	curl -fsSL -o "$out.part" "$url"
	echo "$sum  $out.part" | sha256sum -c --status || { echo "checksum mismatch: $url" >&2; rm -f "$out.part"; exit 1; }
	mv "$out.part" "$out"
}
vda=https://github.com/Ciantic/VirtualDesktopAccessor
fetch "$vda/releases/download/2024-12-16-windows11/VirtualDesktopAccessor.dll" "$QSW_TOOLCHAIN/vda/VirtualDesktopAccessor.dll" \
	8740c572a1c000e3b87ffeb1e4c397eae9af3bd4a2abdc3bcffacab4493f8ff5
fetch "$vda/releases/download/2019-windows10/VirtualDesktopAccessor.dll" "$QSW_TOOLCHAIN/vda/win10/VirtualDesktopAccessor.dll" \
	145431bc2090281838de31824bc829dd35688b13488f4850f4bc5f25f5e599da
curl -fsSL -o "$QSW_TOOLCHAIN/vda/LICENSE.txt" "https://raw.githubusercontent.com/Ciantic/VirtualDesktopAccessor/rust/LICENSE.txt"

if ! ls "$QSW_TOOLCHAIN"/vcredist/vcruntime140.dll >/dev/null 2>&1; then
	echo "note: put the x64 Visual C++ runtime DLLs in $QSW_TOOLCHAIN/vcredist (see WINDOWS.md) or the package will need the redistributable installed" >&2
fi

echo "toolchain ready in $QSW_TOOLCHAIN"
