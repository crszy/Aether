pragma Singleton
import QtQuick
import Quickshell

// Windows: end-4's ConflictKiller looks for (and can kill) other Linux tray and notification daemons. Inside
// Caelestia there is nothing to look for, and it must never kill anything: this stand-in does nothing.
Singleton {
    function load() {}
}
