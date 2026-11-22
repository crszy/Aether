// A title-bar button: the style draws it (children), this gives it hovered / down and the click.
import QtQuick

Item {
    id: root

    readonly property bool hovered: area.containsMouse
    readonly property bool down: area.pressed
    default property alias content: holder.data

    signal clicked

    Item {
        id: holder

        anchors.fill: parent
    }

    MouseArea {
        id: area

        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
}
