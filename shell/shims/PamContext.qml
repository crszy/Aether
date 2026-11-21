import QtQuick
import Quickshell.Windows as Win

// Windows stand-in for Quickshell.Services.Pam's PamContext, used by the ported shells' lock screens (Zenith,
// DMS). There is no PAM; by the config the lock asks for:
//   - password configs: start() asks for the password, respond() checks it with Quickshell.Windows' PasswordCheck -
//     the same check Caelestia's lock uses (your Caelestia lock PIN, else your Windows password)
//   - fingerprint configs ("fprint", "finger"): Quickshell.Windows' Fingerprint (Windows Biometric Framework, the
//     fingerprints set up in Windows Hello) - start() waits for a finger
//   - security-key configs ("u2f"): Aether's own key (AetherKey: a registered USB stick) - start() waits for it
//   - face ("howdy", "face"): not on Windows, reports an error
QtObject {
    id: root

    property string config: "login"
    property string configDirectory: ""
    property string user: ""
    property bool active: false
    property string message: ""
    property bool messageIsError: false
    property bool responseRequired: false
    property bool responseVisible: false

    signal pamMessage
    signal completed(int result)
    signal error(int error)

    property bool _checking: false
    property bool _scanning: false
    readonly property string _kind: /fprint|finger/i.test(config) ? "fingerprint" : /u2f/i.test(config) ? "key"
        : /howdy|face/i.test(config) ? "none" : "password"

    function _fail(): bool {
        Qt.callLater(() => {
            root.error(1);
            root.completed(PamResult.Error);
        });
        return false;
    }

    function start(): bool {
        messageIsError = false;
        responseVisible = false;
        if (_kind === "none")
            return _fail();
        if (_kind === "fingerprint") {
            if (!Win.Fingerprint.available)
                return _fail();
            active = true;
            message = "Touch the fingerprint sensor";
            pamMessage();
            _scanning = true;
            Win.Fingerprint.verify();
            return true;
        }
        if (_kind === "key") {
            if (!AetherKey.registered)
                return _fail();
            active = true;
            message = "Plug in your Aether key";
            pamMessage();
            AetherKey.watching = true;
            if (AetherKey.present)
                Qt.callLater(() => root._keyOk());
            return true;
        }
        active = true;
        message = "Password:";
        responseRequired = false;
        responseRequired = true;   // a change every start, so "on responseRequired" handlers run
        pamMessage();
        return true;
    }

    function respond(text: string): void {
        if (!active || _checking || _kind !== "password")
            return;
        responseRequired = false;
        _checking = true;
        Win.PasswordCheck.verify(text);
    }

    function abort(): void {
        _checking = false;
        active = false;
        responseRequired = false;
        if (_scanning) {
            _scanning = false;
            Win.Fingerprint.cancel();
        }
        if (_kind === "key")
            AetherKey.watching = false;
    }

    function _keyOk(): void {
        if (!active || _kind !== "key")
            return;
        active = false;
        AetherKey.watching = false;
        completed(PamResult.Success);
    }

    property Connections _check: Connections {
        target: Win.PasswordCheck
        function onFinished(ok, msg) {
            if (!root._checking)
                return;
            root._checking = false;
            root.active = false;
            if (!ok) {
                root.message = msg || "Authentication failed";
                root.messageIsError = true;
            }
            root.completed(ok ? PamResult.Success : PamResult.Failed);
        }
    }

    property Connections _finger: Connections {
        target: Win.Fingerprint
        function onFinished(ok, msg) {
            if (!root._scanning)
                return;
            root._scanning = false;
            root.active = false;
            if (!ok) {
                root.message = msg || "Fingerprint not recognized";
                root.messageIsError = true;
            }
            root.completed(ok ? PamResult.Success : PamResult.Failed);
        }
    }

    property Connections _key: Connections {
        target: AetherKey
        function onFound() { root._keyOk(); }
    }
}
