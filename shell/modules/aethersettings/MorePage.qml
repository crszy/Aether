pragma ComponentBehavior: Bound
// More: the things that still live in other windows (your own shells, the store, configs), and each shell's own
// settings window.
import QtQuick
import qs.services as Cae
import qs.modules.rice as Rice

PageScroll {
    id: page

    property var win: null

    readonly property var links: [
        { icon: "folder_special", name: "Your shells", desc: "Shells you made or imported", act: () => Rice.RiceFactory.open("yours") },
        { icon: "storefront", name: "Store", desc: "Get more shells, looks and parts", act: () => Rice.RiceFactory.open("store") },
        { icon: "description", name: "Config files", desc: "Every settings file, raw", act: () => Rice.RiceFactory.open("configs") },
        { icon: "restart_alt", name: "Setup again", desc: "Run the first-start picker again", act: () => Rice.RiceFactory.open("setup") },
        { icon: "tune", name: "Caelestia settings", desc: "Caelestia's own settings window", act: () => Cae.WinSettings.ipc("nexus", "openCaelestia", []) },
        { icon: "settings", name: "Noctalia settings", desc: "Noctalia's own settings window", act: () => Cae.WinSettings.ipc("settings", "open", []) }
    ]

    Repeater {
        model: page.links
        SettingRow {
            id: row
            required property var modelData
            label: modelData.name
            desc: modelData.desc
            reserved: 60
            Icon {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                name: "open_in_new"
                glyph: "↗"
                size: 18
                color: M3.inkDim
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    Sfx.play("click");
                    row.modelData.act();
                }
            }
        }
    }
}
