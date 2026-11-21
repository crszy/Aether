import QtQuick
import QtQuick.Layouts
import Quickshell
import qs.end4.modules.waffle.looks
import qs.end4.modules.waffle.actionCenter

RowLayout {
    id: root

    required property string title
    spacing: 4

    WPanelIconButton {
        iconName: "arrow-left"
        onClicked: ActionCenterContext.back()
    }

    WText {
        id: titleText
        Layout.fillWidth: true
        elide: Text.ElideRight
        text: root.title
        font.pixelSize: Looks.font.pixelSize.large
    }
}
