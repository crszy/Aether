import QtQuick
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.waffle.looks

WBorderedButton {
    id: root
    implicitWidth: 24
    implicitHeight: 24
    contentItem: Item {
        FluentIcon {
            anchors.centerIn: parent
            implicitSize: 12
            icon: root.icon.name
            color: root.fgColor
        }
    }
}
