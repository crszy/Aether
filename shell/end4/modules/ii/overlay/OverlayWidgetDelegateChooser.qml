pragma ComponentBehavior: Bound
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.widgets
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Bluetooth
import qs.end4.modules.ii.overlay.crosshair
import qs.end4.modules.ii.overlay.volumeMixer
import qs.end4.modules.ii.overlay.floatingImage
import qs.end4.modules.ii.overlay.fpsLimiter
import qs.end4.modules.ii.overlay.recorder
import qs.end4.modules.ii.overlay.resources
import qs.end4.modules.ii.overlay.notes

DelegateChooser {
    id: root
    role: "identifier"

    DelegateChoice { roleValue: "crosshair"; Crosshair {} }
    DelegateChoice { roleValue: "floatingImage"; FloatingImage {} }
    DelegateChoice { roleValue: "fpsLimiter"; FpsLimiter {} }
    DelegateChoice { roleValue: "recorder"; Recorder {} }
    DelegateChoice { roleValue: "resources"; Resources {} }
    DelegateChoice { roleValue: "notes"; Notes {} }
    DelegateChoice { roleValue: "volumeMixer"; VolumeMixer {} }
}
