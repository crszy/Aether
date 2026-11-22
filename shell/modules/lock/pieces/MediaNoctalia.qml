// Mix & match piece (WinSettings.lockMedia = "noctalia"): Noctalia's media card on Caelestia's lock.
// Loaded by URL from Content.qml only when picked.
import QtQuick
import Quickshell
import qs.noctalia.Modules.Cards

MediaCard {
    property var screen: Quickshell.screens[0] // the card looks up its screen's wallpaper
}
