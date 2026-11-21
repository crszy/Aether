import QtQuick
import QtQuick.Controls
import "../../../" as Shell

Switch {
    id: control
    indicator: Rectangle {
        implicitWidth: Shell.Theme.scaled(40)
        implicitHeight: Shell.Theme.scaled(20)
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: Shell.Theme.scaled(10)
        color: control.checked ? Shell.Theme.accentColor : Qt.rgba(255, 255, 255, 0.15)
        scale: control.pressed ? 0.95 : 1.0

        Behavior on color { ColorAnimation { duration: Shell.Theme.animInstant } }
        Behavior on scale { NumberAnimation { duration: Shell.Theme.animInstant; easing.type: Easing.OutCubic } }

        Rectangle {
            x: control.checked ? parent.width - width - Shell.Theme.scaled(2) : Shell.Theme.scaled(2)
            y: Shell.Theme.scaled(2)
            width: Shell.Theme.scaled(16); height: Shell.Theme.scaled(16)
            radius: Shell.Theme.scaled(8)
            color: Shell.Theme.text
            Behavior on x { NumberAnimation { duration: Shell.Theme.animFast; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
        }
    }
}
