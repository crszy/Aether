import qs.end4.modules.common
import qs.end4.services as E4Win_
import qs.end4.modules.common.widgets
import qs.end4.services
import Quickshell
import Quickshell.Io

QuickToggleButton {
    id: root
    buttonIcon: "gamepad"
    toggled: toggled

    onClicked: {
        root.toggled = !root.toggled
        if (root.toggled) {
            E4Win_.WinActions.gameMode(true)
        } else {
            E4Win_.WinActions.gameMode(false)
        }
    }
    Process {
        id: fetchActiveState
        running: true
        command: ["bash", "-c", `test "$(hyprctl getoption animations:enabled -j | jq ".int")" -ne 0`]
        onExited: (exitCode, exitStatus) => {
            root.toggled = exitCode !== 0 // Inverted because enabled = nonzero exit
        }
    }
    StyledToolTip {
        text: Translation.tr("Game mode")
    }
}