// SddmShim.qml - qylock's SDDM-compatible interface on Windows (Aether).
//
// Themes bind to sddm.login(), sddm.loginSucceeded / loginFailed, sddm.hostName, userModel, sessionModel, keyboard
// and config (theme.conf), exactly as upstream. The password goes to Quickshell.Windows' PasswordCheck - the same
// check Caelestia's and DMS's locks use on Aether (your Aether lock PIN, else your Windows password). Fingerprint is
// Quickshell.Windows' Fingerprint (Windows Hello fingerprints, no dialog), the security key is Aether's own key (a
// registered USB stick - AetherShims.AetherKey): plugging it in unlocks. The password always works too.

import QtQuick
import Quickshell
import Quickshell.Windows as Win
import AetherShims

Item {
    id: shim

    property string themePath: ""
    property var config: ({})
    property bool configReady: false
    property string hostName: Quickshell.env("COMPUTERNAME") || "localhost"

    // upstream state the themes and overlay read
    property bool fpEnabled: Win.Fingerprint.available
    property bool fpHasFingers: Win.Fingerprint.available
    property bool loginSessionActive: true
    readonly property bool fingerprintReady: Win.Fingerprint.available
    property string fingerprintState: "idle"
    property bool fingerprintUnlock: false
    property bool armWhenReady: true
    property bool unlocked: false
    readonly property bool keyEnabled: AetherKey.registered
    property string keyState: AetherKey.registered ? "touch" : "off"
    property bool keyPasswordMode: true

    property bool checking: false
    property int failures: 0

    onThemePathChanged: loadConfig(themePath)
    Component.onCompleted: loadConfig(themePath)

    function resetAuth() {
        checking = false;
    }

    // theme.conf (key=value) from the theme folder
    function loadConfig(path) {
        if (!path) {
            config = { background: "bg.png" };
            configReady = true;
            return;
        }
        const xhr = new XMLHttpRequest();
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE)
                return;
            const next = {};
            if ((xhr.status === 200 || xhr.status === 0) && xhr.responseText) {
                for (const raw of xhr.responseText.split("\n")) {
                    const line = raw.trim();
                    if (line.startsWith("[") || line === "" || line.startsWith("#"))
                        continue;
                    const parts = line.split("=");
                    if (parts.length === 2)
                        next[parts[0].trim()] = parts[1].trim();
                }
            }
            if (!next.background)
                next.background = "bg.png";
            shim.config = next;
            shim.configReady = true;
        };
        try {
            xhr.open("GET", "file:///" + String(path).replace(/^\/+/, "") + "/theme.conf", true);
            xhr.send();
        } catch (e) {
            shim.config = { background: "bg.png" };
            shim.configReady = true;
        }
    }

    readonly property string userName: Win.PasswordCheck.userName() || Quickshell.env("USERNAME") || "user"

    property var userModel: ListModel {
        id: internalUserModel

        property string lastUser: shim.userName
        property int lastIndex: 0
        function rowCount() { return count; }
        function index(row, col) { return row; }
        function data(row, role) {
            const item = get(row);
            if (!item)
                return "";
            if (role === (Qt.UserRole + 1))
                return item.name;
            if (role === (Qt.UserRole + 2))
                return item.realName;
            return item.name;
        }
        Component.onCompleted: append({ name: shim.userName, realName: shim.userName, icon: "", homeDir: Quickshell.env("USERPROFILE") || "" })
    }

    property var sessionModel: ListModel {
        property int lastIndex: 0
        property string desktopName: "aether"
        function rowCount() { return count; }
        function index(row, col) { return row; }
        function data(row, role) {
            const item = get(row);
            return item ? item.name : "";
        }
        Component.onCompleted: append({ name: "Aether", file: "aether.desktop" })
    }

    property var sddm: QtObject {
        property string hostName: shim.hostName
        signal loginFailed()
        signal loginSucceeded()
        signal keyTimedOut()
        signal surfaceRevealed()
        property bool keyPrompts: false
        readonly property bool keyReady: shim.keyEnabled
        readonly property string keyState: shim.keyState
        readonly property bool keyPasswordMode: true
        readonly property bool fingerprintHint: true
        property bool fingerprintReady: shim.fingerprintReady
        property string fingerprintState: shim.fingerprintState
        property bool fingerprintUnlock: shim.fingerprintUnlock

        function usePassword() {}
        function useKey() {}
        function loginKeyPin(user, pin) { shim.sddm.loginFailed(); }

        function login(user, password, sessionIndex) {
            if (shim.unlocked || shim.checking)
                return;
            if (password === "") {
                shim.sddm.loginFailed();
                return;
            }
            shim.checking = true;
            Win.PasswordCheck.verify(password);
        }

        function reboot() { Quickshell.execDetached(["shutdown.exe", "/r", "/t", "0"]); }
        function powerOff() { Quickshell.execDetached(["shutdown.exe", "/s", "/t", "0"]); }
        function suspend() { Quickshell.execDetached(["rundll32.exe", "powrprof.dll,SetSuspendState", "0,1,0"]); }
    }

    // a finger or the key unlocked: the theme hears loginSucceeded like a typed password
    function unlockBy(viaFinger) {
        if (shim.unlocked)
            return;
        shim.unlocked = true;
        shim.fingerprintUnlock = viaFinger;
        Win.Fingerprint.cancel();
        AetherKey.watching = false;
        shim.sddm.loginSucceeded();
    }

    function armFingerprint() {
        if (shim.unlocked || !Win.Fingerprint.available || Win.Fingerprint.scanning)
            return;
        shim.fingerprintState = "scanning";
        Win.Fingerprint.verify();
    }

    Connections {
        target: shim.sddm
        function onSurfaceRevealed() {
            shim.armFingerprint();
            AetherKey.watching = AetherKey.registered;
        }
    }

    Connections {
        target: Win.Fingerprint
        function onFinished(ok, msg) {
            if (shim.unlocked)
                return;
            if (ok) {
                shim.fingerprintState = "success";
                shim.unlockBy(true);
                return;
            }
            if (msg === "cancelled")
                return;
            shim.fingerprintState = "fail";
            fpRetry.restart();
        }
    }

    Timer {
        id: fpRetry
        interval: 900
        onTriggered: shim.armFingerprint()
    }

    Connections {
        target: AetherKey
        function onFound() { shim.unlockBy(false); }
    }

    property var keyboard: QtObject {
        property bool numLock: false
        property bool capsLock: false
    }

    // a miss waits a little longer each time (upstream backs off the same way)
    Timer {
        id: retryGate
        interval: Math.min(8000, 400 * Math.pow(2, Math.max(0, shim.failures - 3)))
        onTriggered: shim.checking = false
    }

    Connections {
        target: Win.PasswordCheck
        function onFinished(ok, msg) {
            if (!shim.checking)
                return;
            if (ok) {
                shim.unlocked = true;
                shim.failures = 0;
                shim.checking = false;
                shim.sddm.loginSucceeded();
            } else {
                shim.failures++;
                shim.sddm.loginFailed();
                if (shim.failures > 3)
                    retryGate.restart();
                else
                    shim.checking = false;
            }
        }
    }
}
