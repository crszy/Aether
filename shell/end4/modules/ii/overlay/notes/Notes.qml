import QtQuick
import QtQuick.Layouts
import Quickshell
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.ii.overlay

StyledOverlayWidget {
    id: root
    title: Translation.tr("Notes")
    showCenterButton: true

    contentItem: NotesContent {
        radius: root.contentRadius
        isClickthrough: root.clickthrough
    }
}
