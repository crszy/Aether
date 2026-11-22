import QtQuick
import QtQuick.Layouts
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.waffle.looks
import qs.end4.modules.waffle.actionCenter

BodyRectangle {
    id: root
    implicitHeight: contentLayout.implicitHeight

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        spacing: 0

        MainPageBodyToggles {
            id: togglesContainer
            Layout.fillWidth: true
        }

        Rectangle {
            implicitHeight: 1
            Layout.fillWidth: true
            color: Looks.colors.bg1Border
        }

        MainPageBodySliders {
            Layout.margins: 12
            Layout.topMargin: 18
            Layout.bottomMargin: 14
        }
    }
}
