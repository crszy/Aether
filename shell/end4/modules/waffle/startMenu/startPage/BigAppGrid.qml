pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.common.widgets
import qs.end4.modules.waffle.looks

GridLayout {
    id: root

    property list<var> desktopEntries: []

    columnSpacing: 0
    rowSpacing: 0

    uniformCellHeights: true
    uniformCellWidths: true

    Repeater {
        model: root.desktopEntries
        delegate: StartAppButton {
            id: pinnedAppButton
            required property var modelData
            desktopEntry: modelData
            onClicked: {
                GlobalStates.searchOpen = false;
                desktopEntry.execute();
            }
        }
    }
}
