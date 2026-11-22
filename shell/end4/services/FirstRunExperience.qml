pragma Singleton
import QtQuick
import Quickshell

// Windows: no first-run welcome window or default-wallpaper switch inside Caelestia (Caelestia sets the wallpaper)
Singleton {
    property string firstRunFilePath: ""
    function load() {}
    function enableNextTime() {}
    function disableNextTime() {}
    function handleFirstRun() {}
}
