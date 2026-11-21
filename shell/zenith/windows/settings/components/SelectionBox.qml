import QtQuick
import QtQuick.Controls
import "../../../" as Shell

Rectangle {
    id: control
    width: Shell.Theme.scaled(150)
    height: Shell.Theme.scaled(32)
    radius: Shell.Theme.scaled(8)
    color: Shell.Theme.surface1
    
    property var model: []
    property int currentIndex: 0
    signal activated(int index)

    Text {
        anchors.centerIn: parent
        text: control.model[control.currentIndex]
        color: Shell.Theme.text
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            // Simplified selection logic for prototype
            control.currentIndex = (control.currentIndex + 1) % control.model.length
            control.activated(control.currentIndex)
        }
    }
}
