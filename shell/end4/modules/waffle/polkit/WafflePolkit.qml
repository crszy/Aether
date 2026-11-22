import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.widgets
import qs.end4.modules.common.functions
import QtQuick
import Quickshell
import Quickshell.Wayland

FullscreenPolkitWindow {
    id: root
    contentComponent: Component {
        WPolkitContent {}
    }
}
