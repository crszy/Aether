import QtQml
import Quickshell.Windows

// Windows stand-in for Quickshell.Services.Pam.PamContext.
// The "passwd"/"login" style configs ask once for a password and check it against the signed-in Windows
// account (PasswordCheck -> LogonUser). Configs Windows has no equivalent for (fingerprint, howdy, ...)
// finish with an Error straight away, as before.
QtObject {
    id: root

    property bool active: false
    property string config: "login"
    property string configDirectory: "/etc/pam.d"
    property string user: ""
    property string message: ""
    property bool messageIsError: false
    property bool responseRequired: false
    property bool responseVisible: false

    readonly property bool _password: ["passwd", "login", "password", "system-auth", "system-login", "su", "sudo"].indexOf(config) >= 0
    property bool _checking: false

    signal completed(int result)
    signal error(int error)
    signal pamMessage()

    property Timer _unsupported: Timer {
        interval: 50
        onTriggered: {
            root.active = false;
            root.completed(PamResult.Error);
        }
    }

    property Connections _check: Connections {
        target: PasswordCheck
        function onFinished(ok, msg) {
            if (!root._checking) return;
            root._checking = false;
            if (!root.active) return;
            root.active = false;
            root.message = msg;
            root.messageIsError = !ok;
            if (!ok) root.pamMessage();
            root.completed(ok ? PamResult.Success : PamResult.Failed);
        }
    }

    function start() {
        if (root.active) return false;
        root.message = "";
        root.messageIsError = false;
        root.active = true;
        if (!root._password) {
            root._unsupported.start();
            return true;
        }
        root.responseVisible = false;
        root.responseRequired = true;   // ask for the password
        return true;
    }

    function abort() {
        root._unsupported.stop();
        root.responseRequired = false;
        root._checking = false;
        root.active = false;
    }

    function respond(response) {
        if (!root.active || !root.responseRequired) return;
        root.responseRequired = false;
        root._checking = true;
        PasswordCheck.verify(response === undefined || response === null ? "" : response);
    }
}
