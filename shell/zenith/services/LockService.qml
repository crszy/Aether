// services/LockService.qml
//
// State for the session lock: whether it should be engaged, the password
// buffer and the PAM conversation. The lock surfaces (windows/lock) read this
// directly instead of receiving objects through per-screen surface
// properties, which arrived undefined and left the password field wired to
// nothing.
//
// Authenticates against /etc/pam.d/zenith-lock when the system provides it
// (zenith-nixos: security.pam.services.zenith-lock) and the universal "login"
// stack otherwise. Escape hatch from a TTY if PAM is misconfigured:
//     quickshell ipc call zenith:lock unlock
import QtQuick
import qs.shims
import Quickshell
import Quickshell.Io
pragma Singleton

Singleton {
    id: root

    enum State { None, Failed, Error, MaxTries }

    // Desired state. WlSessionLock binds to it; the surfaces clear it once
    // their exit animation has finished.
    property bool locked: false

    // Raised by PAM on success (or the TTY escape hatch); surfaces animate
    // out and then drop `locked`.
    signal unlockRequested()
    signal flash()

    readonly property bool authenticating: passwd.active
    property string buffer: ""
    property int state: LockService.State.None
    property string message: ""

    function lock() {
        if (locked) return;
        MenuService.closeAll();
        reset();
        locked = true;
    }

    function unlock() {
        if (locked) unlockRequested();
    }

    function reset() {
        passwd.abort();
        buffer = "";
        state = LockService.State.None;
        message = "";
    }

    function handleKey(event) {
        if (passwd.active || state === LockService.State.MaxTries) return;

        if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
            if (buffer.length > 0) passwd.start();
        } else if (event.key === Qt.Key_Backspace) {
            buffer = (event.modifiers & Qt.ControlModifier) ? "" : buffer.slice(0, -1);
        } else if (event.key === Qt.Key_Escape) {
            buffer = "";
        } else if (event.text && /^[^\x00-\x1F\x7F-\x9F]+$/.test(event.text)) {
            buffer += event.text;
        }
    }

    function submit() {
        if (buffer.length > 0 && !passwd.active) passwd.start();
    }

    property string pamConfig: "login"
    Process {
        running: true
        command: ["test", "-f", "/etc/pam.d/zenith-lock"]
        onExited: (code) => { if (code === 0) root.pamConfig = "zenith-lock"; }
    }

    PamContext {
        id: passwd
        config: root.pamConfig

        onMessageChanged: {
            if (message.startsWith("The account is locked")) root.message = message;
        }

        onResponseRequiredChanged: {
            if (!responseRequired) return;
            respond(root.buffer);
            root.buffer = "";
        }

        onError: (err) => console.warn("zenith lock: pam error", err, "(config " + root.pamConfig + ")")
        onCompleted: (res) => {
            if (res === PamResult.Success) {
                root.state = LockService.State.None;
                root.unlockRequested();
                return;
            }
            if (res === PamResult.MaxTries) root.state = LockService.State.MaxTries;
            else if (res === PamResult.Error) root.state = LockService.State.Error;
            else root.state = LockService.State.Failed;
            root.flash();
            stateReset.restart();
        }
    }

    Timer {
        id: stateReset
        interval: 4000
        onTriggered: if (root.state !== LockService.State.MaxTries) root.state = LockService.State.None
    }
}
