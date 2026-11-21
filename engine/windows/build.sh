#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/env.sh"

[ -d "$QT_WIN" ] && [ -d "$XWIN/crt" ] || { echo "no toolchain in $QSW_TOOLCHAIN - run windows/toolchain.sh first" >&2; exit 1; }

if [ ! -f "$QSW_BUILD/build.ninja" ]; then
	cmake -S "$QSW" -B "$QSW_BUILD" -G Ninja \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_TOOLCHAIN_FILE="$QSW/windows/cmake/clang-cl-xwin.cmake" \
		-DCLI11_INCLUDE_DIR="$QSW_TOOLCHAIN/cli11/include" \
		-DDISTRIBUTOR="quickshell-windows" \
		-DQS_VERSION_FILE="$QSW/windows/VERSION" \
		-DQS_AUTOSTART_NAME="Quickshell" \
		-DQS_AUTOSTART_DESCRIPTION="Starts Quickshell when you sign in." \
		-DQS_AUTOSTART_ARGS="" \
		"$@"
fi
cmake --build "$QSW_BUILD"
ls -la "$QSW_BUILD/qs.exe" "$QSW_BUILD/qsw.exe"
