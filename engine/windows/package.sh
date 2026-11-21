#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/env.sh"

VERSION="$(tr -d '[:space:]' < "$QSW/windows/VERSION")"
NAME="quickshell-$VERSION-windows-x64"
OUT="$QSW/dist"
D="$OUT/$NAME"

for exe in qs.exe qsw.exe; do
	[ -f "$QSW_BUILD/$exe" ] || { echo "no $QSW_BUILD/$exe - run windows/build.sh first" >&2; exit 1; }
done

rm -rf "$D" "$OUT/$NAME.zip" "$OUT/$NAME.zip.sha256"
mkdir -p "$D"
cp -f "$QSW_BUILD/qs.exe" "$QSW_BUILD/qsw.exe" "$D/"

unused_dll() {
	case "$(basename "$1")" in
	Qt6Designer*.dll | Qt6Help.dll | Qt6Multimedia*.dll | Qt6QmlCompiler.dll | Qt6Quick3DSpatialAudio.dll | \
		Qt6QuickTest.dll | Qt6SpatialAudio.dll | Qt6Test.dll | Qt6UiTools.dll | opengl32sw.dll | d3dcompiler_47.dll | \
		avcodec-*.dll | avformat-*.dll | avutil-*.dll | swresample-*.dll | swscale-*.dll) return 0 ;;
	esac
	return 1
}

for f in "$QT_WIN"/bin/*.dll; do
	if [[ "$f" == *d.dll && -f "${f%d.dll}.dll" ]]; then continue; fi
	unused_dll "$f" && continue
	cp -f "$f" "$D/"
done

copy_tree() {
	local src="$1" dst="$2"
	(cd "$src" && find . -type f ! -name '*.pdb' ! -name '*.lib' ! -name '*.prl' -print0) |
		while IFS= read -r -d '' rel; do
			if [[ "$rel" == *d.dll && -f "$src/${rel%d.dll}.dll" ]]; then continue; fi
			mkdir -p "$dst/$(dirname "$rel")"
			cp -f "$src/$rel" "$dst/$rel"
		done
}

for p in platforms styles imageformats iconengines tls generic networkinformation; do
	[ -d "$QT_WIN/plugins/$p" ] && copy_tree "$QT_WIN/plugins/$p" "$D/plugins/$p"
done
rm -f "$D"/plugins/imageformats/{qicns,qtga,qwbmp}.dll "$D/plugins/platforms/qdirect2d.dll" "$D/plugins/tls/qopensslbackend.dll"
for q in QtQuick QtQml QtCore Qt5Compat QtPositioning Qt; do
	[ -d "$QT_WIN/qml/$q" ] && copy_tree "$QT_WIN/qml/$q" "$D/qml/$q"
done
rm -rf "$D/qml/Qt/test" "$D/qml/QtQuick/Controls/Imagine" "$D/qml/QtQuick/Controls/Universal"

(cd "$QSW/shims" && find . -type f ! -name '*.md' -print0) | while IFS= read -r -d '' f; do
	mkdir -p "$D/qml/$(dirname "$f")"
	cp -f "$QSW/shims/$f" "$D/qml/$f"
done

printf '[Paths]\nPrefix = .\nPlugins = plugins\nQmlImports = qml\n' > "$D/qt.conf"

mkdir -p "$D/licenses"
cp -f "$QSW/LICENSE" "$D/licenses/Quickshell-LGPL-3.0.txt"
cp -f "$QSW/LICENSE-GPL" "$D/licenses/GPL-3.0.txt"
cp -f "$QSW/src/windows/third_party/material_color/LICENSE" "$D/licenses/material-color-utilities-Apache-2.0.txt"
[ -f "$QSW_TOOLCHAIN/cli11/LICENSE" ] && cp -f "$QSW_TOOLCHAIN/cli11/LICENSE" "$D/licenses/CLI11-BSD-3-Clause.txt"
cat > "$D/licenses/Qt.txt" <<TXT
This package includes Qt $QT_VERSION libraries, plugins and QML modules, used under the
GNU Lesser General Public License v3 (see Quickshell-LGPL-3.0.txt and GPL-3.0.txt).
Qt is a trademark of The Qt Company Ltd. Source code for this Qt version:
https://download.qt.io/official_releases/qt/${QT_VERSION%.*}/$QT_VERSION/submodules/
You may replace the Qt DLLs in this folder with your own build of the same version.
TXT

if [ -f "$QSW_TOOLCHAIN/vda/VirtualDesktopAccessor.dll" ]; then
	cp -f "$QSW_TOOLCHAIN/vda/VirtualDesktopAccessor.dll" "$D/"
	mkdir -p "$D/win10"
	cp -f "$QSW_TOOLCHAIN/vda/win10/VirtualDesktopAccessor.dll" "$D/win10/"
	cp -f "$QSW_TOOLCHAIN/vda/LICENSE.txt" "$D/licenses/VirtualDesktopAccessor-MIT.txt"
else
	echo "warning: no VirtualDesktopAccessor.dll - virtual desktop switching falls back to the registry and keystrokes" >&2
fi

if ls "$QSW_TOOLCHAIN"/vcredist/*.dll >/dev/null 2>&1; then
	cp -f "$QSW_TOOLCHAIN"/vcredist/*.dll "$D/"
	cat > "$D/licenses/Microsoft-Visual-C++-runtime.txt" <<TXT
The vcruntime140*.dll and msvcp140*.dll files are the Microsoft Visual C++ runtime,
redistributed next to the application as the Visual Studio license allows.
They come from Microsoft's VC_redist.x64.exe.
TXT
else
	echo "warning: no vcredist DLLs - qs.exe needs the Visual C++ redistributable installed" >&2
fi

mkdir -p "$D/examples"
cp -r "$QSW/examples/." "$D/examples/"
cp -f "$QSW/windows/install.ps1" "$D/"
echo "$VERSION" > "$D/VERSION"

(cd "$OUT" && rm -f "$NAME.zip" && cd "$D" && zip -rq -X "$OUT/$NAME.zip" .)
(cd "$OUT" && sha256sum "$NAME.zip" > "$NAME.zip.sha256")
echo "packaged $OUT/$NAME.zip ($(du -sh "$OUT/$NAME.zip" | cut -f1))"
