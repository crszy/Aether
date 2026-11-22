import QtQuick
import QtQuick.Layouts
import qs.end4.modules.common
import qs.end4.modules.common.widgets

Loader {
    id: root
    required property string icon
    property real iconSize: Appearance.font.pixelSize.larger
    Layout.alignment: Qt.AlignVCenter

    active: root.icon && root.icon.length > 0
    visible: active

    sourceComponent: Item {
        implicitWidth: materialSymbol.implicitWidth

        MaterialSymbol {
            id: materialSymbol
            anchors.centerIn: parent

            iconSize: root.iconSize
            color: root.toggled ? Appearance.colors.colOnPrimary : Appearance.colors.colOnSecondaryContainer
            text: root.icon
        }
    }
}
