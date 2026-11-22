pragma ComponentBehavior: Bound
// About: credits and licences.
import QtQuick
import Quickshell

PageScroll {
    id: page

    readonly property var credits: [
        { name: "Aether", by: "crszy", lic: "AGPL-3.0-or-later", text: "The shell host, the store and the shells around it.", url: "" },
        { name: "DarkShadowShell", by: "shadowww-dev (shadoww.dev)", lic: "MIT", text: "This window's look (the rail, pages, switches, sliders, stars and tap sounds) and the DarkShadowShell shell: its bar, island, menus, desktop widgets and dock. Copyright (c) 2026 shadowww-dev.", url: "https://github.com/shadowww-dev/DarkShadowShell" },
        { name: "serpantinum", by: "Illia Miroshnichenko", lic: "AGPL-3.0", text: "The serpantinum shell and its settings guide. Copyright (C) 2026 Illia Miroshnichenko.", url: "https://github.com/ilyamiro/serpantinum" },
        { name: "Caelestia", by: "caelestia-dots", lic: "GPL-3.0", text: "The base shell this all grew from.", url: "https://github.com/caelestia-dots/shell" },
        { name: "Noctalia", by: "noctalia-dev", lic: "MIT", text: "The Noctalia shell.", url: "https://github.com/noctalia-dev/noctalia-shell" }
    ]

    Repeater {
        model: page.credits
        Rectangle {
            id: card
            required property var modelData
            width: page.width - 48
            height: col.implicitHeight + 32
            radius: M3.rL
            color: M3.surfaceC
            Column {
                id: col
                x: 18
                y: 16
                width: parent.width - 36
                spacing: 4
                Row {
                    spacing: 10
                    T {
                        kind: "title"
                        text: card.modelData.name
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: lic.implicitWidth + 16
                        height: 22
                        radius: 11
                        color: M3.secondaryContainer
                        T {
                            id: lic
                            anchors.centerIn: parent
                            text: card.modelData.lic
                            font.pixelSize: 11
                            color: M3.inkSecondaryC
                        }
                    }
                }
                T {
                    text: "by " + card.modelData.by
                    color: M3.primary
                    font.pixelSize: 12
                }
                T {
                    kind: "reading"
                    width: parent.width
                    text: card.modelData.text
                    color: M3.inkDim
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                Btn {
                    visible: card.modelData.url !== ""
                    variant: "text"
                    text: "Source"
                    sym: "open_in_new"
                    onClicked: Qt.openUrlExternally(card.modelData.url)
                }
            }
        }
    }
}
