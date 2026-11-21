// services/MailService.qml
//
// Owns the mail account: where the credentials live, whether they work, and
// how many unread messages there are.
//
// The password is kept out of anything the UI writes routinely, at
// ~/.config/zenith/mail.json with owner-only permissions -- the same treatment
// the Todoist token gets, and for the same reason.
//
// Reading mail itself is scripts/mail_fetch.py: plain stdlib IMAP, so it can be
// run and checked on its own instead of only ever through the UI.

pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import "../Settings"

QtObject {
    id: service

    readonly property string configDir: PathSettings.configDir + "/zenith"
    readonly property string configPath: configDir + "/mail.json"
    readonly property string script: PathSettings.scriptsDir + "/mail_fetch.py"

    property bool hasConfig: false
    property bool connected: false
    property bool offline: false
    property bool authFailed: false
    property bool checking: false
    property int unread: 0
    property string account: ""
    property string lastError: ""

    signal accountSaved()
    signal accountRejected(string reason)

    // Most people only know their address, so the server is derived from it and
    // only asked for when the guess would be wrong.
    function hostForAddress(addr) {
        var at = String(addr || "").split("@");
        if (at.length < 2) return "imap.gmail.com";
        var domain = at[1].toLowerCase();
        if (domain === "gmail.com" || domain === "googlemail.com") return "imap.gmail.com";
        if (domain === "outlook.com" || domain === "hotmail.com"
            || domain === "live.com" || domain === "msn.com")   return "outlook.office365.com";
        if (domain === "yahoo.com" || domain === "ymail.com")   return "imap.mail.yahoo.com";
        if (domain === "icloud.com" || domain === "me.com")     return "imap.mail.me.com";
        if (domain === "proton.me" || domain === "protonmail.com") return "127.0.0.1"; // bridge
        return "imap." + domain;
    }

    function shellQuote(str) { return "'" + String(str).replace(/'/g, "'\\''") + "'"; }

    // append=true keeps the existing accounts and adds another; the merge is
    // done by a tiny python step because it has to read the current file, and
    // shelling out a JSON rewrite from QML would be worse.
    function saveAccount(user, password, hostOverride, append) {
        var u = String(user || "").trim();
        // Google presents an app password as four groups of four with spaces
        // ("abcd efgh ijkl mnop"). Pasted verbatim that fails to authenticate,
        // so the spaces come out here rather than becoming a support puzzle.
        var p = String(password || "").replace(/\s+/g, "");
        if (u === "" || p === "") {
            lastError = "Address and password are both required.";
            accountRejected(lastError);
            return;
        }

        var payload = JSON.stringify({
            host: (hostOverride && hostOverride !== "") ? hostOverride : hostForAddress(u),
            port: 993,
            user: u,
            password: p,
            mailbox: "INBOX"
        }, null, 2);

        // umask 077 so the file is created 0600 -- it holds a password.
        var merge =
            'import json,os,sys\n' +
            'path=sys.argv[1]; entry=json.loads(sys.argv[2]); append=sys.argv[3]=="1"\n' +
            'accounts=[]\n' +
            'if append:\n' +
            '    try:\n' +
            '        cur=json.load(open(path))\n' +
            '        accounts=cur.get("accounts") if isinstance(cur,dict) and cur.get("accounts") else ([cur] if isinstance(cur,dict) and cur.get("user") else [])\n' +
            '    except Exception:\n' +
            '        accounts=[]\n' +
            'accounts=[a for a in accounts if a.get("user")!=entry["user"]]\n' +
            'accounts.append(entry)\n' +
            'os.makedirs(os.path.dirname(path), exist_ok=True)\n' +
            'json.dump({"accounts":accounts}, open(path,"w"), indent=2)\n';

        writeProc.command = ["sh", "-c",
            'umask 077; mkdir -p ' + shellQuote(configDir) +
            ' && python3 -c "$1" ' + shellQuote(configPath) + ' "$2" "$3"',
            "--", merge, payload, append ? "1" : "0"];
        writeProc.running = false;
        writeProc.running = true;
    }

    function clearAccount() {
        hasConfig = false; connected = false; unread = 0; account = "";
        clearProc.running = false;
        clearProc.running = true;
    }

    function checkNow() {
        if (checking) return;
        checking = true;
        statusProc.running = false;
        statusProc.running = true;
    }

    property Process _writeProc: Process {
        id: writeProc
        onExited: (code) => {
            if (code === 0) {
                service.hasConfig = true;
                service.authFailed = false;
                service.accountSaved();
                service.checkNow();
            } else {
                service.lastError = "Could not write " + service.configPath;
                service.accountRejected(service.lastError);
            }
        }
    }

    property Process _clearProc: Process {
        id: clearProc
        command: ["sh", "-c", "rm -f " + service.shellQuote(service.configPath)]
    }

    property Process _statusProc: Process {
        id: statusProc
        command: ["python3", service.script, "status"]
        stdout: StdioCollector {
            onStreamFinished: {
                service.checking = false;
                var res = {};
                try { res = JSON.parse(String(text).trim().split("\n").pop()); }
                catch (e) { service.lastError = "Mail check produced no usable result."; return; }

                if (res.type === "status") {
                    service.hasConfig = true;
                    service.connected = true;
                    service.offline = false;
                    service.authFailed = false;
                    service.unread = res.unread || 0;
                    service.account = res.user || "";
                    service.lastError = "";
                } else {
                    service.connected = false;
                    service.offline = !!res.offline;
                    service.authFailed = !!res.auth;
                    service.hasConfig = !res.needs_config;
                    service.lastError = res.message || "Mail check failed.";
                }
            }
        }
        onExited: (code) => { if (code !== 0) service.checking = false; }
    }

    // hasConfig is set by the mail tab from the `accounts` command when it
    // first opens. There is deliberately no startup probe: it was a shell fork
    // before anything was on screen just to test a file's existence, and
    // reading mail.json into QML to answer the same question would pull the
    // account passwords into the UI process for no reason.
}
