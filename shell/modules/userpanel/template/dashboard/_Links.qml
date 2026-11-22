// An extra dashboard tab (Caelestia's dashboard). Switched off: rename to Links.qml. Every file in dashboard/
// not starting with "_" becomes a tab, titled with its name.
import QtQuick
import QtQuick.Layouts
import qs.components
import qs.services

ColumnLayout {
    spacing: 10

    StyledText {
        text: "Quick links"
        color: Colours.palette.m3onSurface
        font.pointSize: 14
        font.weight: 600
    }

    Repeater {
        // change these to your own
        model: [
            { label: "GitHub", url: "https://github.com" },
            { label: "Quickshell docs", url: "https://quickshell.org/docs/" },
            { label: "r/unixporn", url: "https://reddit.com/r/unixporn" }
        ]

        Rectangle {
            required property var modelData

            implicitWidth: 360
            implicitHeight: 44
            radius: 14
            color: area.containsMouse ? Colours.palette.m3secondaryContainer : Colours.palette.m3surfaceContainerHigh

            StyledText {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 16
                text: modelData.label
                color: Colours.palette.m3onSurface
            }

            MouseArea {
                id: area

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Qt.openUrlExternally(modelData.url)
            }
        }
    }
}
