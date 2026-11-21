import "center"
import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services
import qs.modules.userpanel

ColumnLayout {
    id: root

    required property var lock
    readonly property real centerScale: Math.min(1, (lock.screen?.height ?? 1440) / 1440)
    readonly property int centerWidth: Tokens.sizes.lock.centerWidth * centerScale

    Layout.preferredWidth: centerWidth
    Layout.fillWidth: false
    Layout.fillHeight: true

    spacing: Tokens.spacing.largeIncreased

    // Mix & match (WinSettings.lockClock): another shell's clock piece; Caelestia's own is the fallback
    readonly property var clockPieces: ({ dms: "ClockDms", end4: "ClockEnd4", noctalia: "ClockNoctalia" })

    Clock {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Tokens.padding.large
        centerScale: root.centerScale
        visible: clockPiece.status !== Loader.Ready && !userClock.visible
    }

    Loader {
        id: clockPiece

        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Tokens.padding.large
        scale: root.centerScale
        active: !!root.clockPieces[WinSettings.lockClock]
        source: active ? `pieces/${root.clockPieces[WinSettings.lockClock]}.qml` : ""
        visible: status === Loader.Ready
    }

    // your own clock: "lockClock": "user:<Name>" = ~/.config/caelestia/user/lock/<Name>.qml
    UserPiece {
        id: userClock

        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Tokens.padding.large
        scale: root.centerScale
        kind: "lock"
        name: UserExt.userName(WinSettings.lockClock)
        context: ({ lock: root.lock })
        visible: name !== ""
    }

    StyledText {
        Layout.alignment: Qt.AlignHCenter

        text: Time.format("dddd • d MMM").toUpperCase()
        color: Colours.palette.m3onSurface
        font: Tokens.font.title.builders.medium.weight(Font.DemiBold).build()
    }

    ProfilePic {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Tokens.spacing.extraExtraLarge * root.centerScale
        Layout.bottomMargin: Tokens.spacing.extraLarge * root.centerScale
        centerWidth: root.centerWidth
    }

    PasswordInput {
        Layout.alignment: Qt.AlignHCenter
        centerScale: Math.max(0.8, root.centerScale)
        centerWidth: root.centerWidth
        lock: root.lock
    }

    StateMessage {
        Layout.fillWidth: true
        pam: root.lock.pam
    }
}
