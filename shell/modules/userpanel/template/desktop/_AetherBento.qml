// AetherBento - a block of desktop cards like the rices on r/unixporn: big stacked clock, date, weather, what's
// playing, this month, CPU and memory rings. All colours come from your scheme (so they follow the wallpaper) or
// your skin. Switch it on: rename to AetherBento.qml, and place it in desktop.json:
//   "AetherBento": { "screen": "main", "corner": "bottom-left", "x": 60, "y": 90 }
// Make it yours: drop a card, reorder them, change `cell` (card size) or `gap`.
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import Quickshell.Windows as Win
import Caelestia.Config
import qs.components
import qs.services

GridLayout {
    id: root

    readonly property var pal: AetherStyle.palette
    readonly property real cell: 132          // one card unit
    readonly property real gap: 12
    readonly property color card: Qt.alpha(pal.m3surfaceContainer, 0.86)

    columns: 4
    rowSpacing: gap
    columnSpacing: gap

    Component.onCompleted: {
        Weather.reload();
        Win.SystemMonitor.active = true;
    }

    // ---- big stacked clock (2 x 2) ----
    Card {
        Layout.columnSpan: 2
        Layout.rowSpan: 2

        ColumnLayout {
            anchors.centerIn: parent
            spacing: -34

            StyledText {
                Layout.alignment: Qt.AlignHCenter
                text: Qt.formatTime(Time.date, "hh")
                color: root.pal.m3primary
                font.pointSize: 96
                font.weight: 800
            }
            StyledText {
                Layout.alignment: Qt.AlignHCenter
                text: Qt.formatTime(Time.date, "mm")
                color: root.pal.m3tertiary
                font.pointSize: 96
                font.weight: 800
            }
        }
    }

    // ---- date ----
    Card {
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 0

            StyledText {
                Layout.alignment: Qt.AlignHCenter
                text: Qt.formatDate(Time.date, "ddd").toUpperCase()
                color: root.pal.m3onSurfaceVariant
                font.pointSize: 12
                font.weight: 600
            }
            StyledText {
                Layout.alignment: Qt.AlignHCenter
                text: Qt.formatDate(Time.date, "d")
                color: root.pal.m3onSurface
                font.pointSize: 40
                font.weight: 700
            }
            StyledText {
                Layout.alignment: Qt.AlignHCenter
                text: Qt.formatDate(Time.date, "MMMM")
                color: root.pal.m3primary
                font.pointSize: 11
            }
        }
    }

    // ---- weather ----
    Card {
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 2

            MaterialIcon {
                Layout.alignment: Qt.AlignHCenter
                text: Weather.icon
                color: root.pal.m3tertiary
                fill: 1
                fontStyle: Tokens.font.icon.extraLarge
            }
            StyledText {
                Layout.alignment: Qt.AlignHCenter
                text: Weather.temp
                color: root.pal.m3onSurface
                font.pointSize: 18
                font.weight: 700
            }
            StyledText {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: root.cell - 20
                text: Weather.description
                color: root.pal.m3onSurfaceVariant
                elide: Text.ElideRight
                font.pointSize: 9
            }
        }
    }

    // ---- CPU and memory rings ----
    Card {
        RowLayout {
            anchors.centerIn: parent
            spacing: 10

            Ring {
                readonly property real v: Number(Win.SystemMonitor.cpuUsage) || 0
                value: v > 1 ? v / 100 : v
                label: "CPU"
                colour: root.pal.m3primary
            }
            Ring {
                value: (Number(Win.SystemMonitor.memoryUsed) || 0) / Math.max(1, Number(Win.SystemMonitor.memoryTotal) || 1)
                label: "RAM"
                colour: root.pal.m3tertiary
            }
        }
    }

    // ---- this month ----
    Card {
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 4

            StyledText {
                Layout.alignment: Qt.AlignHCenter
                text: Qt.formatDate(Time.date, "MMMM")
                color: root.pal.m3onSurface
                font.pointSize: 9
                font.weight: 600
            }
            Grid {
                id: month

                readonly property date first: new Date(Time.date.getFullYear(), Time.date.getMonth(), 1)
                readonly property int lead: (first.getDay() + 6) % 7          // Monday first
                readonly property int days: new Date(Time.date.getFullYear(), Time.date.getMonth() + 1, 0).getDate()

                columns: 7
                spacing: 1

                Repeater {
                    model: month.lead + month.days

                    Rectangle {
                        required property int index
                        readonly property int day: index - month.lead + 1
                        readonly property bool today: day === Time.date.getDate()

                        width: 14
                        height: 12
                        radius: 4
                        color: today ? root.pal.m3primary : "transparent"

                        StyledText {
                            anchors.centerIn: parent
                            visible: parent.day > 0
                            text: parent.day
                            color: parent.today ? root.pal.m3onPrimary : root.pal.m3onSurfaceVariant
                            font.pointSize: 6
                        }
                    }
                }
            }
        }
    }

    // ---- what's playing (the full width) ----
    Card {
        id: media

        readonly property var player: Players.active
        Layout.columnSpan: 4

        RowLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 12

            Rectangle {
                Layout.preferredWidth: media.height - 28
                Layout.preferredHeight: media.height - 28
                radius: 18
                color: root.pal.m3secondaryContainer
                clip: true

                MaterialIcon {
                    anchors.centerIn: parent
                    visible: !art.source.toString()
                    text: "music_note"
                    color: root.pal.m3onSecondaryContainer
                    fontStyle: Tokens.font.icon.extraLarge
                }
                Image {
                    id: art

                    anchors.fill: parent
                    source: media.player ? Players.getArtUrl(media.player) : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 200
                    sourceSize.height: 200
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                StyledText {
                    Layout.fillWidth: true
                    text: media.player?.trackTitle || "Nothing playing"
                    color: root.pal.m3onSurface
                    elide: Text.ElideRight
                    font.pointSize: 11
                    font.weight: 700
                }
                StyledText {
                    Layout.fillWidth: true
                    text: media.player?.trackArtist ?? ""
                    color: root.pal.m3onSurfaceVariant
                    elide: Text.ElideRight
                    font.pointSize: 9.5
                }
                RowLayout {
                    spacing: 14

                    Repeater {
                        model: [["skip_previous", () => media.player?.previous()], [media.player?.isPlaying ? "pause" : "play_arrow", () => media.player?.togglePlaying()], ["skip_next", () => media.player?.next()]]

                        MaterialIcon {
                            required property var modelData
                            text: modelData[0]
                            color: root.pal.m3primary
                            fill: 1
                            fontStyle: Tokens.font.icon.medium

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -6
                                cursorShape: Qt.PointingHandCursor
                                onClicked: parent.modelData[1]()
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- the pieces ----
    component Card: Rectangle {
        Layout.preferredWidth: root.cell * (Layout.columnSpan || 1) + root.gap * ((Layout.columnSpan || 1) - 1)
        Layout.preferredHeight: root.cell * (Layout.rowSpan || 1) + root.gap * ((Layout.rowSpan || 1) - 1)
        radius: 28
        color: root.card
        border.width: 1
        border.color: Qt.alpha(root.pal.m3outlineVariant, 0.5)
    }

    component Ring: Item {
        id: ring

        property real value: 0
        property string label
        property color colour

        implicitWidth: 50
        implicitHeight: 50

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: Qt.alpha(ring.colour, 0.2)
                strokeWidth: 5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc { centerX: 25; centerY: 25; radiusX: 21; radiusY: 21; startAngle: 0; sweepAngle: 360 }
            }
            ShapePath {
                strokeColor: ring.colour
                strokeWidth: 5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc { centerX: 25; centerY: 25; radiusX: 21; radiusY: 21; startAngle: -90; sweepAngle: 360 * Math.max(0, Math.min(1, ring.value)) }
            }
        }

        StyledText {
            anchors.centerIn: parent
            text: ring.label
            color: root.pal.m3onSurfaceVariant
            font.pointSize: 7
            font.weight: 700
        }
    }
}
