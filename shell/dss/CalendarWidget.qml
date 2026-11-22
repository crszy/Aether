// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

Item {
    id: cal
    property real bg: 1   // card background opacity (widget editor)
    property var pal: null
    readonly property color cFg: pal ? pal.fg : "#F6F5F4"
    property date now: new Date()
    property int off: 0
    readonly property var days: Tr.week(false)

    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate()
    }
    function monthStart() { return new Date(now.getFullYear(), now.getMonth() + off, 1) }
    function title() { return monthStart().toLocaleDateString(Tr.loc, "MMMM yyyy") }
    function cells() {
        const m = monthStart()
        const lead = (m.getDay() + 6) % 7
        const out = []
        for (let i = 0; i < 42; i++) {
            const c = new Date(m.getFullYear(), m.getMonth(), 1 - lead + i)
            out.push({ d: c.getDate(), cur: c.getMonth() === m.getMonth(), today: sameDay(c, now) })
        }
        return out
    }

    Timer { interval: 30000; running: true; repeat: true; onTriggered: cal.now = new Date() }

    Card { anchors.fill: parent; k: cal.bg; pal: cal.pal }

    Row {
        id: head
        x: 24
        y: 20
        width: parent.width - 48
        T {
            kind: "title"
            text: cal.title()
            font.pixelSize: 17
            font.bold: true
            color: cal.cFg
            width: parent.width - 76
            anchors.verticalCenter: parent.verticalCenter
        }
        Repeater {
            model: [{ g: "‹", d: -1 }, { g: "›", d: 1 }]
            delegate: Rectangle {
                width: 34
                height: 34
                radius: 17
                color: navMouse.containsMouse ? (cal.pal ? cal.pal.chipHover : Qt.rgba(1, 1, 1, 0.14)) : (cal.pal ? cal.pal.chip : Qt.rgba(1, 1, 1, 0.07))
                Behavior on color { ColorAnimation { duration: 140 } }
                T { anchors.centerIn: parent; text: modelData.g; font.pixelSize: 18; color: cal.cFg }
                MouseArea {
                    id: navMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    property bool sfxOwn: true
                    onClicked: { Sfx.play(modelData.d > 0 ? "page-next" : "page-prev"); cal.off += modelData.d }
                }
            }
        }
    }

    Grid {
        id: grid
        x: 24
        y: 72
        columns: 7
        columnSpacing: 0
        rowSpacing: 4
        readonly property real cellW: (cal.width - 48) / 7

        Repeater {
            model: cal.days
            delegate: Item {
                width: grid.cellW
                height: 24
                T { anchors.centerIn: parent; text: modelData; font.pixelSize: 12; color: cal.pal ? cal.pal.fg3 : "#8a8a8f" }
            }
        }
        Repeater {
            model: cal.cells()
            delegate: Item {
                width: grid.cellW
                height: 34
                // today in your shape
                Blob {
                    anchors.centerIn: parent
                    width: 32; height: 32
                    visible: modelData.today
                    kind: Look.shape
                    color: cal.pal ? cal.pal.primary : "#F6F5F4"
                    SlowSpin { turn: 360; period: 30000; running: (modelData.today && !Look.lite) && !Look.covered }
                }
                T {
                    kind: "number"
                    anchors.centerIn: parent
                    text: modelData.d
                    font.pixelSize: 13
                    font.bold: modelData.today
                    color: modelData.today ? (cal.pal ? cal.pal.inkOn : "#0d0d0d") : (modelData.cur ? cal.cFg : (cal.pal ? cal.pal.fg4 : "#55555a"))
                }
            }
        }
    }
}
