pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import Quickshell
import Quickshell.Wayland
import Caelestia.Config
import Caelestia.Multimedia
import Caelestia.State
import qs.components
import qs.services
import qs.modules.userpanel

WlSessionLockSurface {
    id: root

    required property WlSessionLock lock
    required property Pam pam

    readonly property alias unlocking: unlockAnim.running
    // a monitor that only stays locked while showing the live desktop (WinSettings.lockLiveOthers)
    readonly property bool liveHere: lock.isLive?.(screen) ?? false
    // your own lock screen: windows.json "mixLock": "user:<Name>" = ~/.config/caelestia/user/lock/<Name>.qml
    readonly property string userLockName: WinSettings.distroMode ? "distro" : UserExt.userName(WinSettings.mixLock)
    // a whole Linux distro brings its own lock screen (distros/<family>/...Lock.qml)
    readonly property string distroLock: WinSettings.distroMode ? Quickshell.shellPath("distros/gnome/GnomeLock.qml") : ""

    contentItem.Config.screen: screen.name
    contentItem.Tokens.screen: screen.name

    color: "transparent"

    Connections {
        function onUnlock(): void {
            unlockAnim.start();
        }

        target: root.lock
    }

    SequentialAnimation {
        id: unlockAnim

        ParallelAnimation {
            Anim {
                target: lockContent
                properties: "implicitWidth,implicitHeight"
                to: lockContent.size
            }
            Anim {
                target: lockBg
                property: "radius"
                to: lockContent.radius
            }
            Anim {
                target: content
                property: "scale"
                to: 0
            }
            Anim {
                target: content
                property: "opacity"
                to: 0
                type: Anim.StandardSmall
            }
            Anim {
                target: lockIcon
                property: "opacity"
                to: 1
                type: Anim.StandardLarge
            }
            Anim {
                target: background
                property: "opacity"
                to: 0
                type: Anim.StandardLarge
            }
            Anim {
                target: liveLayer
                property: "opacity"
                to: 0
                type: Anim.StandardLarge
            }
            Anim {
                target: userLock
                property: "opacity"
                to: 0
                type: Anim.StandardLarge
            }
            SequentialAnimation {
                PauseAnimation {
                    duration: Tokens.anim.durations.small
                }
                Anim {
                    type: Anim.Standard
                    target: lockContent
                    property: "opacity"
                    to: 0
                }
            }
        }
        PropertyAction {
            target: root.lock
            property: "locked"
            value: false
        }
    }

    ParallelAnimation {
        id: initAnim

        running: true

        Anim {
            target: background
            property: "opacity"
            to: 1
            type: Anim.StandardLarge
        }
        Anim {
            target: liveLayer
            property: "opacity"
            to: 1
            type: Anim.StandardLarge
        }
        Anim {
            target: userLock
            property: "opacity"
            to: 1
            type: Anim.StandardLarge
        }
        SequentialAnimation {
            ParallelAnimation {
                Anim {
                    target: lockContent
                    property: "scale"
                    to: 1
                    type: Anim.FastSpatial
                }
                Anim {
                    target: lockContent
                    property: "rotation"
                    to: 360
                    duration: Tokens.anim.durations.expressiveFastSpatial
                    easing: Tokens.anim.standardAccel
                }
            }
            ParallelAnimation {
                Anim {
                    target: lockIcon
                    property: "rotation"
                    to: 360
                    easing: Tokens.anim.standardDecel
                }
                Anim {
                    type: Anim.DefaultEffects
                    target: lockIcon
                    property: "opacity"
                    to: 0
                }
                Anim {
                    type: Anim.DefaultEffects
                    target: content
                    property: "opacity"
                    to: 1
                }
                Anim {
                    target: content
                    property: "scale"
                    to: 1
                }
                Anim {
                    target: lockBg
                    property: "radius"
                    to: lockContent.Tokens.rounding.extraLarge * 1.5
                }
                Anim {
                    target: lockContent
                    property: "implicitWidth"
                    to: (root.screen?.height ?? 0) * lockContent.Tokens.sizes.lock.heightMult * lockContent.Tokens.sizes.lock.ratio
                }
                Anim {
                    target: lockContent
                    property: "implicitHeight"
                    to: (root.screen?.height ?? 0) * lockContent.Tokens.sizes.lock.heightMult
                }
            }
        }
    }

    Item {
        id: background

        anchors.fill: parent
        opacity: 0
        visible: !root.liveHere

        layer.enabled: true
        layer.effect: MultiEffect {
            autoPaddingEnabled: false
            blurEnabled: WinSettings.lockBlur > 0
            blur: WinSettings.lockBlur
            blurMax: 48
            blurMultiplier: 1
        }

        Loader {
            anchors.fill: parent
            sourceComponent: Config.lock.useWallpaper ? wallpaperBackground : screencopyBackground
        }
    }

    Component {
        id: screencopyBackground

        // Windows: the lock grabs each screen just before it appears (live capture is refused on some accounts)
        Image {
            source: root.lock.snapshotFor(root.screen)
            fillMode: Image.PreserveAspectCrop
            asynchronous: false
            cache: false
        }
    }

    Component {
        id: wallpaperBackground

        Visual {
            source: Wallpapers.current ? "file:///" + Wallpapers.current.split("\\").join("/") : ""
            playing: PersistentState.wallpaperPlaying && !WinSettings.saving
        }
    }

    // live monitor: the desktop stays visible behind a light dimming, with a small "locked" pill
    Item {
        id: liveLayer

        anchors.fill: parent
        visible: root.liveHere
        opacity: 0

        Rectangle {
            anchors.fill: parent
            color: "black"
            opacity: Math.max(0, Math.min(0.9, WinSettings.lockLiveDim))
        }

        StyledRect {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Tokens.padding.extraLarge * 2
            implicitWidth: livePill.implicitWidth + Tokens.padding.large * 2
            implicitHeight: livePill.implicitHeight + Tokens.padding.medium * 2
            radius: Tokens.rounding.full
            color: Qt.alpha(Colours.palette.m3surface, 0.85)

            Row {
                id: livePill

                anchors.centerIn: parent
                spacing: Tokens.spacing.small

                MaterialIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "lock"
                    color: Colours.palette.m3primary
                }
                StyledText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Locked")
                    color: Colours.palette.m3onSurface
                }
            }
        }
    }

    Item {
        id: lockContent

        readonly property int size: lockIcon.implicitHeight + Tokens.padding.large * 4
        readonly property int radius: size / 4 * Tokens.rounding.scale

        anchors.centerIn: parent
        implicitWidth: size
        implicitHeight: size

        visible: Config.lock.enabled && !root.liveHere && root.userLockName === ""
        rotation: 180
        scale: 0

        StyledRect {
            id: lockBg

            anchors.fill: parent
            color: Colours.palette.m3surface
            radius: parent.radius
            opacity: Colours.transparency.enabled ? Colours.transparency.base : 1

            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                blurMax: 15
                shadowColor: Qt.alpha(Colours.palette.m3shadow, 0.7)
            }
        }

        MaterialIcon {
            id: lockIcon

            anchors.centerIn: parent
            text: "lock"
            fontStyle: Tokens.font.icon.builders.extraLarge.scale(4).weight(Font.Bold).build()
            rotation: 180
        }

        Content {
            id: content

            anchors.centerIn: parent
            width: (root.screen?.height ?? 0) * Tokens.sizes.lock.heightMult * Tokens.sizes.lock.ratio - Tokens.padding.extraLargeIncreased
            height: (root.screen?.height ?? 0) * Tokens.sizes.lock.heightMult - Tokens.padding.extraLargeIncreased

            lock: root
            opacity: 0
            scale: 0
        }
    }

    // your own lock screen over the (blurred) desktop. Typing goes to the password check as usual; the file shows
    // what it likes from `pam` (pam.buffer = what was typed, pam.state, pam.lockMessage) and `lock` (lock.unlocking)
    Item {
        id: userLock

        anchors.fill: parent
        visible: root.userLockName !== "" && !root.liveHere
        opacity: 0
        focus: visible
        onVisibleChanged: if (visible) forceActiveFocus()
        Component.onCompleted: if (visible) forceActiveFocus()
        onActiveFocusChanged: if (visible && !activeFocus) forceActiveFocus()
        Keys.onPressed: event => {
            if (!root.unlocking)
                root.pam.handleKey(event);
        }

        UserPiece {
            anchors.fill: parent
            fill: true
            kind: "lock"
            visible: !root.distroLock
            name: userLock.visible && !root.distroLock ? root.userLockName : ""
            context: ({ pam: root.pam, lock: root, screen: root.screen })
        }
        Loader {
            anchors.fill: parent
            active: userLock.visible && !!root.distroLock
            source: root.distroLock
            onLoaded: {
                item.pam = root.pam;
                item.lock = root;
                item.screen = root.screen;
            }
        }
    }
}
