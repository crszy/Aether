pragma ComponentBehavior: Bound
// Home: a hero card in DarkShadowShell's style (greeting, your look), shortcuts into the rail, and the switches
// people look for first.
import QtQuick
import Quickshell
import qs.services as Cae
import "catalog.js" as Cat

PageScroll {
    id: page

    property var win: null
    property date now: new Date()
    readonly property var activeLook: Cat.activeLook(Cae.WinSettings)

    Timer {
        interval: 30000
        running: page.visible
        repeat: true
        onTriggered: page.now = new Date()
    }

    // hero
    Rectangle {
        width: parent.width
        height: 168
        radius: M3.rXL
        color: M3.primaryContainer
        clip: true

        Rectangle {   // a soft blob of the key colour behind the text
            width: 260
            height: 260
            radius: 130
            x: parent.width - 200
            y: -70
            color: M3.alpha(M3.primary, 0.22)
        }
        Rectangle {
            width: 140
            height: 140
            radius: 70
            x: parent.width - 330
            y: 90
            color: M3.alpha(M3.tertiary, 0.18)
        }
        Column {
            anchors.left: parent.left
            anchors.leftMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            T {
                text: Qt.formatDate(page.now, "dddd, d MMMM").toUpperCase()
                color: M3.inkPrimaryC
                font.pixelSize: 11
                font.letterSpacing: 1.2
                font.weight: Font.Medium
            }
            T {
                kind: "title"
                text: {
                    const h = page.now.getHours();
                    return h < 5 ? "Good night" : h < 12 ? "Good morning" : h < 18 ? "Good afternoon" : "Good evening";
                }
                font.pixelSize: 34
                font.weight: Font.DemiBold
                color: M3.ink
            }
            T {
                kind: "reading"
                text: page.activeLook ? "Your desktop is the " + page.activeLook.name + " look." : "Your desktop is your own mix of shells."
                color: M3.inkPrimaryC
                font.pixelSize: 13
            }
        }
        ShellLogo {   // the look you are on (Aether's live mark for your own mix)
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.top: parent.top
            anchors.topMargin: 20
            size: 56
            shell: page.activeLook ? page.activeLook.id : "aether"
            icon: page.activeLook ? page.activeLook.icon : "auto_awesome"
            colour: page.activeLook ? page.activeLook.colour : M3.primary
            chosen: true
        }
        Btn {
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            variant: "filled"
            text: "Change the look"
            sym: "auto_awesome"
            onClicked: page.win && page.win.go("shells", "looks")
        }
    }

    // three plain steps for people new to Linux shells
    SectionTitle {
        text: "New here? Three steps"
        sub: "Nothing you change here can break Windows - every step can be undone the same way."
    }
    Row {
        id: steps
        width: parent.width
        spacing: 10
        Repeater {
            model: [
                { n: "1", title: "Pick a look", text: "One click dresses the whole desktop like a shell.", go: ["shells", "looks"], sym: "auto_awesome" },
                { n: "2", title: "Swap single parts", text: "Like one shell's bar but another's dock? Mix them.", go: ["shells", "mix"], sym: "tune" },
                { n: "3", title: "Fine-tune", text: "Each part's page shows the options of the shell you use.", go: ["bar", "bar"], sym: "toolbar" }
            ]
            Rectangle {
                id: step
                required property var modelData
                width: (steps.width - steps.spacing * 2) / 3
                height: 112
                radius: stepArea.containsMouse ? M3.rL : M3.rM
                color: stepArea.containsMouse ? M3.surfaceHigh : M3.surfaceC
                Behavior on radius { NumberAnimation { duration: M3.d(220); easing.type: M3.springy } }
                Behavior on color { ColorAnimation { duration: 140 } }
                Rectangle {
                    x: 16
                    y: 16
                    width: 30
                    height: 30
                    radius: 15
                    color: M3.primaryContainer
                    T {
                        anchors.centerIn: parent
                        text: step.modelData.n
                        font.pixelSize: 14
                        font.weight: Font.Bold
                        color: M3.inkPrimaryC
                    }
                }
                T {
                    x: 56
                    y: 21
                    width: parent.width - 72
                    text: step.modelData.title
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                T {
                    kind: "reading"
                    x: 16
                    y: 56
                    width: parent.width - 32
                    text: step.modelData.text
                    color: M3.inkDim
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                MouseArea {
                    id: stepArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.win && page.win.go(step.modelData.go[0], step.modelData.go[1])
                }
            }
        }
    }

    SectionTitle {
        text: "Jump to"
    }
    Grid {
        id: tiles
        width: parent.width
        columns: Math.max(2, Math.min(4, Math.floor(width / 200)))
        columnSpacing: 10
        rowSpacing: 10
        Repeater {
            model: Cat.groups.filter(g => g.id !== "home")
            Rectangle {
                id: tile
                required property var modelData
                width: (tiles.width - tiles.columnSpacing * (tiles.columns - 1)) / tiles.columns
                height: 86
                radius: tileArea.containsMouse ? M3.rL : M3.rM
                color: tileArea.containsMouse ? M3.surfaceHigh : M3.surfaceC
                Behavior on radius { NumberAnimation { duration: M3.d(220); easing.type: M3.springy } }
                Behavior on color { ColorAnimation { duration: 140 } }
                Rectangle {
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44
                    height: 44
                    radius: 22
                    color: M3.secondaryContainer
                    Icon {
                        anchors.centerIn: parent
                        name: tile.modelData.icon
                        size: 20
                        color: M3.inkSecondaryC
                    }
                }
                Column {
                    x: 72
                    width: parent.width - 84
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    T {
                        text: tile.modelData.name
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }
                    T {
                        width: parent.width
                        text: tile.modelData.tabs.map(t => t.name).join(" · ")
                        color: M3.inkDim
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
                MouseArea {
                    id: tileArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.win && page.win.go(tile.modelData.id, "")
                }
            }
        }
    }

    SectionTitle {
        text: "Quick switches"
    }
    ToggleRow {
        label: "Hide the Windows taskbar"
        desc: "Your bar takes over; Windows' slides in at the screen edge"
        checked: Cae.WinSettings.hideTaskbar
        onToggled: v => Cae.WinSettings.hideTaskbar = v
    }
    SegRow {
        label: "Aether colours"
        desc: "This window, your panels, the splash: the Aether purple, or your colour scheme"
        options: [{ value: "aether", label: "Aether purple" }, { value: "scheme", label: "My scheme" }]
        current: Cae.WinSettings.aetherColours
        onChosen: v => Cae.WinSettings.aetherColours = v
    }
    ToggleRow {
        label: "Battery saver"
        desc: "Laptops: pause video wallpapers while on battery"
        checked: Cae.WinSettings.batterySaver === "auto"
        onToggled: v => Cae.WinSettings.batterySaver = v ? "auto" : "off"
    }
}
