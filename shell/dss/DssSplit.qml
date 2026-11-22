// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows stand-in for SplitParser (DssProc feeds it line by line), added by dotfiles/dss-port.js.
import QtQuick

QtObject {
    property string splitMarker: "\n"
    signal read(string data)
}
