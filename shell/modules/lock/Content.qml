import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services
import qs.modules.userpanel

RowLayout {
    id: root

    required property var lock

    spacing: Tokens.spacing.largeIncreased * 2

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Tokens.spacing.medium

        WeatherInfo {
            Layout.fillWidth: true
            rootHeight: root.height
        }

        Fetch {
            Layout.fillWidth: true
            rootHeight: root.height
        }

        Media {
            Layout.fillWidth: true
            Layout.fillHeight: true
            lock: root.lock
            visible: mediaPiece.status !== Loader.Ready && !userMedia.visible
        }

        // Mix & match (WinSettings.lockMedia): another shell's media card; Caelestia's own is the fallback
        Loader {
            id: mediaPiece

            readonly property var pieces: ({ dms: "MediaDms", end4: "MediaEnd4", noctalia: "MediaNoctalia" })

            Layout.fillWidth: true
            Layout.fillHeight: true
            active: !!pieces[WinSettings.lockMedia]
            source: active ? `pieces/${pieces[WinSettings.lockMedia]}.qml` : ""
            visible: status === Loader.Ready
        }

        // your own media card: "lockMedia": "user:<Name>" = ~/.config/caelestia/user/lock/<Name>.qml
        UserPiece {
            id: userMedia

            Layout.fillWidth: true
            Layout.fillHeight: true
            fill: true
            kind: "lock"
            name: UserExt.userName(WinSettings.lockMedia)
            context: ({ lock: root.lock })
            visible: name !== ""
        }
    }

    Center {
        lock: root.lock
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Tokens.spacing.medium

        Resources {
            Layout.fillWidth: true
        }

        StyledRect {
            Layout.fillWidth: true
            Layout.fillHeight: true

            bottomRightRadius: Tokens.rounding.extraLarge
            radius: Tokens.rounding.medium
            color: Colours.tPalette.m3surfaceContainer

            NotifDock {
                lock: root.lock
            }
        }
    }
}
