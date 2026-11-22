import qs.end4.modules.common.widgets
import qs.end4.services

QuickToggleButton {
    id: root
    toggled: Idle.inhibit
    buttonIcon: "coffee"
    onClicked: {
        Idle.toggleInhibit()
    }
    StyledToolTip {
        text: Translation.tr("Keep system awake")
    }

}
