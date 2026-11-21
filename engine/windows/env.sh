QSW="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export QSW
export QSW_TOOLCHAIN="${QSW_TOOLCHAIN:-$QSW/.toolchain}"
export QT_VERSION="${QT_VERSION:-6.11.2}"
export QT_WIN="$QSW_TOOLCHAIN/qt/$QT_VERSION/msvc2022_64"
export QT_HOST="$QSW_TOOLCHAIN/qt/$QT_VERSION/gcc_64"
export XWIN="$QSW_TOOLCHAIN/xwin"
export QSW_BUILD="${QSW_BUILD:-$QSW/build/windows}"
export PATH="$PATH:$QSW_TOOLCHAIN/bin"
