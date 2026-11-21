import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.services.network
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.common.widgets
import qs.end4.modules.waffle.looks
import qs.end4.modules.waffle.actionCenter

WChoiceButton {
    id: root

    property bool expanded: false
    checked: expanded
    clip: true

    horizontalPadding: 12
    verticalPadding: 6
    animateChoiceHighlight: false

    Behavior on implicitHeight {
        animation: Looks.transition.resize.createObject(this)
    }
    onClicked: expanded = !expanded
}
