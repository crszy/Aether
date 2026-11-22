import QtQuick
import qs.end4.services
import qs.end4.modules.waffle.looks

OSDValue {
    id: root
    iconName: WIcons.volumeIcon
    value: Audio.sink?.audio.volume ?? 0

    Connections {
        // Listen to volume changes
        target: Audio.sink?.audio ?? null
        function onVolumeChanged() {
            if (Audio.ready)
                root.timer.restart();
        }
        function onMutedChanged() {
            if (Audio.ready)
                root.timer.restart();
        }
    }
}
