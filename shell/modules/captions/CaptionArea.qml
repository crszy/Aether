// What makes a header a title bar: press to bring the app forward and drag it (to the top of the screen to
// maximise), double-click to maximise / restore, right-click for the app's window menu, top edge to resize.
// Put it first in a style so its buttons sit above it.
import QtQuick

Item {
    id: root

    required property var skin

    anchors.fill: parent

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onPressed: event => {
            root.skin.activate();
            if (event.button === Qt.LeftButton)
                root.skin.startMove();
        }
        onClicked: event => {
            if (event.button === Qt.RightButton)
                root.skin.showMenu();
        }
        onDoubleClicked: event => {
            if (event.button === Qt.LeftButton)
                root.skin.toggleMaximize();
        }
    }

    MouseArea {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 4
        visible: root.skin.canMaximize && !root.skin.maximized
        cursorShape: Qt.SizeVerCursor
        onPressed: {
            root.skin.activate();
            root.skin.startResize();
        }
    }
}
