import QtQuick
import qs.end4.services as E4Win_
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.waffle.looks
import qs.end4.modules.waffle.bar.tray

BarIconButton {
    id: root

    visible: Updates.updateAdvised || Updates.updateStronglyAdvised
    padding: 4
    iconName: "arrow-sync"
    iconSize: 20 // Needed because the icon appears to have some padding
    iconMonochrome: true
    tooltipText: Translation.tr("Get the latest features and security improvements with\nthe newest feature update.\n\n%1 packages").arg(Updates.count)

    onClicked: {
        E4Win_.WinActions.open(Config.options.apps.update);
    }

    overlayingItems: Rectangle {
        anchors {
            right: parent.right
            bottom: parent.bottom
            margins: 1
        }
        implicitWidth: 8
        implicitHeight: implicitWidth
        radius: height / 2
        color: Updates.updateStronglyAdvised ? Looks.colors.warning : Looks.colors.accent
    }
}
