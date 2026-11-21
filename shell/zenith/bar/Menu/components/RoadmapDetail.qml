// The panel that opens when you click a node, the way roadmap.sh opens one.
//
// Lifted out of RoadmapContent, which had grown past 40KB and was the largest
// file in the repo. This part is self-contained -- it renders whatever it is
// handed and reports back through signals -- so it had no business living
// inside the graph view.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "../../../"
import "../../../services"
import "../../../Settings"

Rectangle {
    id: detail

    property string topicTitle: ""
    property string body: ""
    property var resources: []
    property bool loading: false
    property bool isDone: false

    signal closed()
    signal toggleDone()
    signal openLink(string url)

    color: Theme.cardBackground
    radius: Theme.bubbleRadiusMedium
    border.color: Theme.glassBorder
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.scaled(14)
        spacing: Theme.scaled(10)

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)

            Rectangle {
                width: Theme.scaled(46); height: Theme.scaled(24)
                radius: Theme.scaled(8)
                color: backM.containsMouse ? Theme.cardBackgroundHover : Theme.cardBackground
                border.width: 0
                Text {
                    anchors.centerIn: parent; text: "back"
                    color: Theme.subtext1
                    font.pixelSize: Theme.scaled(9); font.weight: Font.Bold
                }
                MouseArea {
                    id: backM; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: detail.closed()
                }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: doneLabel.implicitWidth + Theme.scaled(22)
                height: Theme.scaled(24)
                radius: Theme.scaled(8)
                color: detail.isDone ? Theme.accentColor
                     : (doneM.containsMouse ? Theme.cardBackgroundHover : Theme.cardBackground)
                border.width: 0
                Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                Text {
                    id: doneLabel
                    anchors.centerIn: parent
                    text: detail.isDone ? "done" : "mark done"
                    color: detail.isDone ? Theme.base : Theme.subtext1
                    font.pixelSize: Theme.scaled(9); font.weight: Font.Black
                }
                MouseArea {
                    id: doneM; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: detail.toggleDone()
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: detail.topicTitle
            color: Theme.text
            font.pixelSize: Theme.scaled(16)
            font.weight: Font.Black
            wrapMode: Text.WordWrap
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: detailCol.implicitHeight
            clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            FastWheel {}

            ColumnLayout {
                id: detailCol
                width: parent.width
                spacing: Theme.scaled(10)

                Text {
                    Layout.fillWidth: true
                    text: detail.loading ? "Loading..." : detail.body
                    color: Theme.subtext1
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
                    lineHeight: 1.25
                }

                Text {
                    Layout.fillWidth: true
                    visible: detail.resources.length > 0
                    text: "RESOURCES"
                    color: Theme.subtext0
                    font.pixelSize: Theme.scaled(9)
                    font.weight: Font.Black
                    font.letterSpacing: 1
                }

                Repeater {
                    model: detail.resources
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: resRow.implicitHeight + Theme.scaled(14)
                        radius: Theme.scaled(8)
                        color: resM.containsMouse ? Theme.cardBackgroundHover : Theme.cardBackground
                        border.color: Theme.cardBorder; border.width: 1

                        RowLayout {
                            id: resRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: Theme.scaled(10)
                            anchors.rightMargin: Theme.scaled(10)
                            spacing: Theme.scaled(8)

                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                width: kindLabel.implicitWidth + Theme.scaled(12)
                                height: Theme.scaled(16)
                                radius: Theme.scaled(4)
                                color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g,
                                               Theme.accentColor.b, 0.18)
                                Text {
                                    id: kindLabel
                                    anchors.centerIn: parent
                                    text: modelData.kind
                                    color: Theme.accentColor
                                    font.pixelSize: Theme.scaled(8)
                                    font.weight: Font.Black
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.label
                                color: Theme.text
                                font.pixelSize: Theme.scaled(10)
                                wrapMode: Text.WordWrap
                            }
                        }

                        MouseArea {
                            id: resM
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: detail.openLink(modelData.url)
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: !detail.loading && detail.resources.length === 0
                             && detail.body !== ""
                    text: "No links published for this topic."
                    color: Theme.subtext0
                    font.pixelSize: Theme.scaled(10)
                }
            }
        }
        }
}
