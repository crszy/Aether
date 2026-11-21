// lock_shell.qml - Ryoku's qylock lock screen on Windows (Aether).
//
// Upstream locks through a Wayland session lock plus login1 proofs (lock.sh / unlock.sh, a daemon handshake). On
// Windows the lock is Quickshell's WlSessionLock (as Caelestia's and DMS's locks) and the password is checked by the
// Windows stand-in shim (shim/SddmShim.qml -> Quickshell.Windows PasswordCheck: your Aether lock PIN, else your
// Windows password). Ctrl+Alt+Del still reaches Windows. The theme (default clockwork/orbital) and its look are
// upstream's. Started by `ryoku-shell lock` (AetherCompat.RyokuCompat) as its own Quickshell instance.

import QtQuick
import Quickshell
import Quickshell.Wayland
import Quickshell.Io
import "./shim"
import Ryoku.Ui.Singletons

ShellRoot {
    id: shellRoot

    property string activeTheme: Quickshell.env("QS_THEME") || "clockwork/orbital"
    property string themePath: Quickshell.env("QS_THEME_PATH") || (String(Quickshell.shellDir).replace(/\\/g, "/") + "/themes/" + activeTheme)

    readonly property var sddm: sddmShim.sddm
    readonly property var config: {
        const merged = {};
        const themeConfig = sddmShim.config || {};
        for (const key in themeConfig)
            merged[key] = themeConfig[key];
        merged.clock24h = TimeFormat.is24h ? "true" : "false";
        return merged;
    }
    readonly property var userModel: sddmShim.userModel
    readonly property var sessionModel: sddmShim.sessionModel
    readonly property var keyboard: sddmShim.keyboard
    property bool sessionLocked: true

    SddmShim {
        id: sddmShim
        themePath: shellRoot.themePath
    }

    // the clockwork theme plays its wind-up before the reveal: wait for it, then drop the lock and quit
    Connections {
        target: sddmShim.sddm
        function onLoginSucceeded() {
            let delay = 100;
            if (shellRoot.activeTheme.includes("clockwork") && sddmShim.config.enableWindup === "true")
                delay = 720;
            quitTimer.interval = delay;
            quitTimer.start();
        }
    }

    Timer {
        id: quitTimer
        onTriggered: {
            shellRoot.sessionLocked = false;
            Qt.quit();
        }
    }

    Component {
        id: themeComponent

        Loader {
            anchors.fill: parent
            focus: true
            source: "file:///" + shellRoot.themePath + "/Main.qml"
            onLoaded: {
                if (item.clock24h !== undefined)
                    item.clock24h = Qt.binding(() => TimeFormat.is24h);
                if (item.clockLocale !== undefined)
                    item.clockLocale = Qt.binding(() => TimeFormat.locale);
                Qt.callLater(() => forceActiveFocus(Qt.OtherFocusReason));
                Qt.callLater(() => sddmShim.sddm.surfaceRevealed());
            }
            onStatusChanged: {
                if (status === Loader.Error)
                    console.error("qylock: could not load the theme", source);
            }
        }
    }

    // ── fingerprint overlay (upstream's, above any theme): scan / unlocked / not recognized ──
    property color fpAccent: "#ffb59b"
    FileView {
        id: paletteFile
        path: (Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").replace(/\\/g, "/") + "/.cache/ryoku/colors.json"
        blockLoading: true
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            try {
                const o = JSON.parse(paletteFile.text() || "{}");
                if (o && typeof o.primary === "string" && o.primary.length)
                    shellRoot.fpAccent = o.primary;
            } catch (e) {}
        }
    }
    Component {
        id: fpOverlayComponent
        Item {
            id: ov
            anchors.fill: parent
            z: 10000
            readonly property var s: sddmShim.sddm
            readonly property string ph: !s.fingerprintReady ? "off"
                : (s.fingerprintState === "idle" ? "ready" : s.fingerprintState)

            FingerprintScan {
                id: fpScan
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.60
                sizePx: Math.round(Math.min(parent.width, parent.height) * 0.10)
                accent: shellRoot.fpAccent
                phase: ov.ph
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: fpScan.bottom
                anchors.topMargin: Math.round(fpScan.sizePx * 0.18)
                font.pixelSize: Math.round(fpScan.sizePx * 0.18)
                color: ov.ph === "fail" ? "#e0806f" : shellRoot.fpAccent
                opacity: (ov.ph === "scanning" || ov.ph === "success" || ov.ph === "fail") ? 0.92 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }
                text: ov.ph === "success" ? I18n.tr("Unlocked") : (ov.ph === "fail" ? I18n.tr("Not recognized") : I18n.tr("Touch the sensor"))
                visible: opacity > 0.01
            }
            // Aether key: a hint while a registered key is not plugged in
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Math.round(parent.height * 0.04)
                font.pixelSize: Math.round(Math.min(parent.width, parent.height) * 0.016)
                color: shellRoot.fpAccent
                opacity: sddmShim.keyEnabled ? 0.7 : 0
                visible: opacity > 0.01
                text: I18n.tr("Plug in your Aether key to unlock")
            }
        }
    }

    WlSessionLock {
        id: lock

        locked: shellRoot.sessionLocked

        surface: Component {
            WlSessionLockSurface {
                id: lockSurface

                color: "black"

                function focusTheme() {
                    Qt.callLater(function() {
                        if (lockSurface.visible && themeSurface.status === Loader.Ready)
                            themeSurface.forceActiveFocus(Qt.OtherFocusReason);
                    });
                }

                onVisibleChanged: if (visible) focusTheme()
                Component.onCompleted: focusTheme()

                // gestures and wheel stay on the lock
                PinchHandler { target: null }
                WheelHandler { target: null }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                    hoverEnabled: true
                    onWheel: wheel => wheel.accepted = true
                }

                Loader {
                    id: themeSurface

                    anchors.fill: parent
                    focus: true
                    sourceComponent: themeComponent
                    onLoaded: lockSurface.focusTheme()
                    onActiveFocusChanged: if (!activeFocus) lockSurface.focusTheme()
                }
                Loader {
                    anchors.fill: parent
                    sourceComponent: fpOverlayComponent
                }
            }
        }
    }
}
