import QtQuick
import QtQuick.Layouts
import qs.services as Cae
import Caelestia.State as CaeState
import qs.components.effects

// GNOME's date menu: notifications on the left (Do Not Disturb + Clear at the bottom), today and the month
// calendar on the right.
Rectangle {
    id: root

    property date shown: new Date(Cae.Time.date.getFullYear(), Cae.Time.date.getMonth(), 1)

    property size screenSize: Qt.size(1920, 1080)   // the menu window covers the screen (origin 0,0)

    width: 720
    height: 420
    radius: Adw.menuRadius
    color: Adw.glass ? "transparent" : Adw.popover
    border.color: Adw.glass ? Qt.rgba(1, 1, 1, 0.14) : Adw.border

    // Liquid Glass option: the menu becomes glass over the wallpaper
    Loader {
        anchors.fill: parent
        z: -1
        active: Adw.glass
        sourceComponent: LiquidGlass {
            radius: root.radius
            source: Adw.wallpaper
            screenSize: root.screenSize
            tint: Qt.rgba(0.06, 0.06, 0.07, 0.5)
        }
    }

    MouseArea { anchors.fill: parent }   // clicks inside don't close the menu

    RowLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 14

        // ---- notifications
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: Cae.Notifs.notClosed.length === 0
                Column {
                    anchors.centerIn: parent
                    spacing: 10
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "notifications"
                        font.family: Adw.icons
                        font.pixelSize: 64
                        color: Adw.dim
                        opacity: 0.6
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "No Notifications"
                        font.family: Adw.font
                        font.pixelSize: 17
                        font.weight: Font.Bold
                        color: Adw.dim
                    }
                }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: Cae.Notifs.notClosed.length > 0
                clip: true
                spacing: 6
                model: Cae.Notifs.notClosed.slice().reverse()
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: col.implicitHeight + 20
                    radius: 14
                    color: hover.containsMouse ? Adw.cardHover : Adw.card
                    MouseArea {
                        id: hover
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                    Column {
                        id: col
                        anchors.left: parent.left
                        anchors.right: close.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 14
                        spacing: 2
                        Text {
                            width: parent.width
                            text: `${modelData.appName}  ·  ${modelData.timeStr}`
                            color: Adw.dim
                            font.family: Adw.font
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: modelData.summary
                            color: Adw.text
                            font.family: Adw.font
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            visible: text.length > 0
                            text: modelData.body
                            color: Adw.text
                            font.family: Adw.font
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                    }
                    Text {
                        id: close
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 10
                        text: "close"
                        font.family: Adw.icons
                        font.pixelSize: 16
                        color: Adw.dim
                        visible: hover.containsMouse || closeArea.containsMouse
                        MouseArea {
                            id: closeArea
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            onClicked: modelData.close()
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "Do Not Disturb"
                    color: Adw.text
                    font.family: Adw.font
                    font.pixelSize: 14
                }
                GSwitch {
                    checked: CaeState.PersistentState.doNotDisturb
                    onToggled: CaeState.PersistentState.doNotDisturb = !CaeState.PersistentState.doNotDisturb
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    implicitWidth: clearText.implicitWidth + 28
                    implicitHeight: 32
                    radius: 8
                    color: clearArea.containsMouse ? Adw.cardHover : Adw.card
                    opacity: Cae.Notifs.notClosed.length > 0 ? 1 : 0.5
                    Text {
                        id: clearText
                        anchors.centerIn: parent
                        text: "Clear"
                        color: Adw.text
                        font.family: Adw.font
                        font.pixelSize: 14
                        font.weight: Font.Bold
                    }
                    MouseArea {
                        id: clearArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: Cae.Notifs.list.slice().forEach(n => n.close())
                    }
                }
            }
        }

        Rectangle {
            Layout.fillHeight: true
            implicitWidth: 1
            color: Adw.border
        }

        // ---- today + calendar
        ColumnLayout {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            spacing: 4

            Text {
                text: Qt.formatDate(Cae.Time.date, "dddd")
                color: Adw.dim
                font.family: Adw.font
                font.pixelSize: 13
                font.weight: Font.Bold
            }
            Text {
                text: Qt.formatDate(Cae.Time.date, "MMMM d yyyy")
                color: Adw.text
                font.family: Adw.font
                font.pixelSize: 19
                font.weight: Font.Bold
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 10
                RoundButton { glyph: "chevron_left"; onClicked: root.shown = new Date(root.shown.getFullYear(), root.shown.getMonth() - 1, 1) }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: Qt.formatDate(root.shown, root.shown.getFullYear() === Cae.Time.date.getFullYear() ? "MMMM" : "MMMM yyyy")
                    color: Adw.text
                    font.family: Adw.font
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }
                RoundButton { glyph: "chevron_right"; onClicked: root.shown = new Date(root.shown.getFullYear(), root.shown.getMonth() + 1, 1) }
            }

            Grid {
                id: grid
                Layout.alignment: Qt.AlignHCenter
                columns: 7
                spacing: 2
                readonly property int first: (new Date(root.shown.getFullYear(), root.shown.getMonth(), 1).getDay() + 7 - Qt.locale().firstDayOfWeek % 7) % 7
                Repeater {
                    model: 7
                    Text {
                        required property int index
                        width: 40
                        height: 26
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: Qt.locale().dayName((index + Qt.locale().firstDayOfWeek) % 7, Locale.NarrowFormat)
                        color: Adw.dim
                        font.family: Adw.font
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                }
                Repeater {
                    model: 42
                    Rectangle {
                        required property int index
                        readonly property date day: new Date(root.shown.getFullYear(), root.shown.getMonth(), index - grid.first + 1)
                        readonly property bool inMonth: day.getMonth() === root.shown.getMonth()
                        readonly property bool today: day.toDateString() === Cae.Time.date.toDateString()
                        width: 40
                        height: 34
                        radius: 17
                        color: today ? Adw.accent : dayArea.containsMouse ? Adw.cardHover : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: day.getDate()
                            color: Adw.text
                            opacity: parent.inMonth ? (day.getDay() === 0 || day.getDay() === 6 ? 0.7 : 1) : 0.3
                            font.family: Adw.font
                            font.pixelSize: 13
                            font.weight: parent.today ? Font.Bold : Font.Normal
                        }
                        MouseArea {
                            id: dayArea
                            anchors.fill: parent
                            hoverEnabled: true
                        }
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    component RoundButton: Rectangle {
        property string glyph
        signal clicked
        implicitWidth: 30
        implicitHeight: 30
        radius: 15
        color: rbArea.containsMouse ? Adw.cardHover : "transparent"
        Text {
            anchors.centerIn: parent
            text: parent.glyph
            font.family: Adw.icons
            font.pixelSize: 18
            color: Adw.text
        }
        MouseArea {
            id: rbArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: parent.clicked()
        }
    }
}
