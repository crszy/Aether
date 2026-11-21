// A bar widget: your avatar initial in a circle. Click: opens panels/profile.qml.
// Put it in a bar with the id "user:Profile" (see README.txt).
import QtQuick
import qs.components
import qs.services

Rectangle {
    property bool vertical: true    // Aether fills this in: true in a side bar

    implicitWidth: 30
    implicitHeight: 30
    radius: 15
    color: area.containsMouse ? Colours.palette.m3primary : Colours.palette.m3primaryContainer

    MaterialIcon {
        anchors.centerIn: parent
        text: "person"
        color: area.containsMouse ? Colours.palette.m3onPrimary : Colours.palette.m3onPrimaryContainer
    }

    MouseArea {
        id: area

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: UserExt.togglePanel("profile")
    }
}
