import QtQuick
import QtQuick.Controls
import "../../../" as Shell

Slider {
    id: control
    implicitWidth: Shell.Theme.scaled(200)
    implicitHeight: Shell.Theme.scaled(10)

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: Shell.Theme.scaled(150)
        implicitHeight: Shell.Theme.scaled(2)
        radius: Shell.Theme.scaled(20)
        color: Shell.Theme.surface1
        
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            color: Shell.Theme.blue
            radius: Shell.Theme.scaled(2)
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: Shell.Theme.scaled(16)
        implicitHeight: Shell.Theme.scaled(16)
        radius: Shell.Theme.scaled(8)
        color: Shell.Theme.text
    }
}
