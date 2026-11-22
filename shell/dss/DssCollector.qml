// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows stand-in for StdioCollector (DssProc fills it), added by dotfiles/dss-port.js.
import QtQuick

QtObject {
    property string text: ""
    property bool waitForEnd: true
    property var data
    signal streamFinished
}
