import QtQuick
import Quickshell
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.common.widgets

StyledText {
    text: "Dialog Title"
    color: Appearance.colors.colOnSurface
    wrapMode: Text.Wrap
    font {
        family: Appearance.font.family.title
        pixelSize: Appearance.font.pixelSize.title
        variableAxes: Appearance.font.variableAxes.title
    }
}
