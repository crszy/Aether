import QtQuick

// libadwaita's switch
Rectangle {
    id: sw
    property bool checked
    signal toggled

    implicitWidth: 46
    implicitHeight: 26
    radius: 13
    color: checked ? Adw.accent : Qt.rgba(1, 1, 1, 0.2)
    Behavior on color { ColorAnimation { duration: 150 } }

    Rectangle {
        width: 20
        height: 20
        radius: 10
        y: 3
        x: sw.checked ? sw.width - width - 3 : 3
        color: "#ffffff"
        Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: sw.toggled()
    }
}
