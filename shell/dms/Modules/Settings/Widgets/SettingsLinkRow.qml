import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets

SettingsRow {
    id: root

    property string url: ""

    resetKeys: []
    clickable: url !== ""
    onClicked: Qt.openUrlExternally(url)

    DIcon {
        anchors.verticalCenter: parent.verticalCenter
        name: "open_in_new"
        size: Theme.iconSize
        color: root.supportingContentColor
    }
}
