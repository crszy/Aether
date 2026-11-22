import QtQuick
import QtQuick.Layouts
import "../../../" as Shell

Rectangle {
    property string label: ""
    default property alias content: rowContent.data
    
    Layout.fillWidth: true
    height: Shell.Theme.scaled(56)
    color: "transparent"
    
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Shell.Theme.scaled(16)
        anchors.rightMargin: Shell.Theme.scaled(16)
        spacing: Shell.Theme.scaled(16)
        
        Text {
            text: label
            color: Shell.Theme.text
            font.pixelSize: Shell.Theme.scaled(15)
            font.weight: Font.Medium
            Layout.fillWidth: true
        }
        
        Item {
            id: rowContent
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
    }
    
    // Bottom border for separation
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width - Shell.Theme.scaled(32)
        anchors.horizontalCenter: parent.horizontalCenter
        height: Shell.Theme.scaled(1)
        color: Shell.Theme.surface1
    }
}
