// Mix & match piece (WinSettings.lockMedia = "end4"): end-4's player control on Caelestia's lock.
// Loaded by URL from Content.qml only when picked. Built only while a player exists (it expects one).
import QtQuick
import qs.end4.services
import qs.end4.modules.ii.mediaControls

Item {
    Loader {
        anchors.fill: parent
        active: !!MprisController.activePlayer

        sourceComponent: PlayerControl {
            player: MprisController.activePlayer
        }
    }
}
