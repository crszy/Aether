import QtQuick
import QtQuick.Controls
import "../../../" as Shell

Row {
    id: control
    spacing: 5

    property int value: 0
    property int from: 0
    property int to: 100
    property int step: 1

    signal valueModified(int value)

    Rectangle {
        width: Shell.Theme.scaled(32)
        height: Shell.Theme.scaled(32)
        radius: Shell.Theme.scaled(8)
        color: Shell.Theme.surface1
        Text {
            anchors.centerIn: parent
            text: "-"
            color: Shell.Theme.text
        }
        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (control.value > control.from) {
                    control.value -= control.step
                    control.valueModified(control.value)
                }
            }
        }
    }

    Rectangle {
        width: Shell.Theme.scaled(50)
        height: Shell.Theme.scaled(32)
        radius: Shell.Theme.scaled(8)
        color: Shell.Theme.surface1
        Text {
            anchors.centerIn: parent
            text: control.value.toString()
            color: Shell.Theme.text
        }
    }

    Rectangle {
        width: Shell.Theme.scaled(32)
        height: Shell.Theme.scaled(32)
        radius: Shell.Theme.scaled(8)
        color: Shell.Theme.surface1
        Text {
            anchors.centerIn: parent
            text: "+"
            color: Shell.Theme.text
        }
        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (control.value < control.to) {
                    control.value += control.step
                    control.valueModified(control.value)
                }
            }
        }
    }
}
