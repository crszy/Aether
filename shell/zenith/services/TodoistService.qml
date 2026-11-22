// services/TodoistService.qml
//
// Owns the Todoist connection: where the API token lives, whether we have one,
// and whether it actually works.
//
// The token is deliberately kept out of the task file. Tasks sync to disk on
// every edit and the task file is meant to be readable (and shareable); a
// secret does not belong in something written that often. It lives on its own
// at ~/.config/zenith/todoist.json with owner-only permissions.

pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import "../Settings"

QtObject {
    id: service

    readonly property string configDir: PathSettings.configDir + "/zenith"
    readonly property string tokenPath: configDir + "/todoist.json"

    // ---- Connection state ----
    property string token: ""
    readonly property bool hasToken: token !== ""

    // Verified against the API at least once this session.
    property bool verified: false
    property bool verifying: false
    property string lastError: ""

    // What the UI asks about: a usable connection.
    readonly property bool connected: hasToken && verified

    signal tokenSaved()
    signal tokenRejected(string reason)

    // Emitted after a sync that changed the local file, so the todo list knows
    // to reload from disk.
    signal tasksChanged()

    // ---- Sync state ----
    property bool syncing: false
    property bool offline: false
    property int pendingChanges: 0
    property double lastSyncAt: 0

    readonly property string syncScript: PathSettings.scriptsDir + "/todoist_sync.py"

    // The merge itself lives in scripts/todoist_sync.py rather than here:
    // it is real logic with real edge cases (push, pull, conflict, offline
    // delete), and in Python it can be exercised against a mock API instead of
    // only ever being tested by hand through the UI.
    function syncNow() {
        if (syncing || !hasToken) return;
        syncing = true;
        syncProc.running = false;
        syncProc.running = true;
    }

    function refreshPending() {
        statusProc.running = false;
        statusProc.running = true;
    }

    property Process _syncProc: Process {
        id: syncProc
        command: ["python3", service.syncScript, "sync"]
        stdout: StdioCollector {
            onStreamFinished: {
                service.syncing = false;
                var res = {};
                try { res = JSON.parse(String(text).trim().split("\n").pop()); }
                catch (e) { service.lastError = "Sync produced no usable result."; return; }

                if (res.type === "sync_done") {
                    service.offline = false;
                    service.verified = true;
                    service.lastError = "";
                    service.lastSyncAt = res.at || 0;
                    service.tasksChanged();
                    service.refreshPending();
                } else {
                    // Offline is expected, not a failure: nothing was written
                    // and the next attempt reconciles.
                    service.offline = !!res.offline;
                    service.lastError = res.message || "Sync failed.";
                    if (res.auth) service.verified = false;
                }
            }
        }
        onExited: (code) => { if (code !== 0) service.syncing = false; }
    }

    property Process _statusProc: Process {
        id: statusProc
        command: ["python3", service.syncScript, "status"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    var res = JSON.parse(String(text).trim());
                    if (res.type === "status") service.pendingChanges = res.pending || 0;
                } catch (e) { /* leave the previous count */ }
            }
        }
    }

    // Retry while there is queued work and a token: this is what turns
    // "offline now" into "synced when the network comes back" without the user
    // having to do anything.
    property Timer _retry: Timer {
        interval: 60000
        repeat: true
        running: service.hasToken
        onTriggered: if (!service.syncing && (service.offline || service.pendingChanges > 0)) service.syncNow()
    }

    function shellQuote(str) {
        return "'" + String(str).replace(/'/g, "'\\''") + "'";
    }

    // Persist the token, then immediately check it against the API so the user
    // finds out it is wrong now rather than the next time a sync silently fails.
    function saveToken(newToken) {
        var clean = String(newToken || "").trim();
        if (clean === "") {
            lastError = "Token is empty.";
            tokenRejected(lastError);
            return;
        }

        var payload = JSON.stringify({ token: clean }, null, 2);

        // umask 077 so the file is created 0600 -- it holds a credential.
        writeProc.command = ["sh", "-c",
            'umask 077; mkdir -p ' + shellQuote(configDir) +
            ' && printf "%s" "$1" > ' + shellQuote(tokenPath),
            "--", payload
        ];
        service._pendingToken = clean;
        writeProc.running = false;
        writeProc.running = true;
    }

    function clearToken() {
        token = "";
        verified = false;
        lastError = "";
        clearProc.running = false;
        clearProc.running = true;
    }

    // Todoist API v1. The older surfaces are gone, not merely deprecated:
    // https://api.todoist.com/rest/v2/...  -> 410 Gone
    // https://api.todoist.com/sync/v9/...  -> 410 Gone
    // Verified by probing them directly -- only /api/v1/* answers (401 for a
    // bad token). Writing this against REST v2, which most documentation and
    // examples still show, would fail on every call with no useful error.
    readonly property string apiBase: "https://api.todoist.com/api/v1"

    // Cheapest authenticated endpoint that proves the token is valid.
    function verifyToken() {
        if (!hasToken || verifying) return;
        verifying = true;
        lastError = "";
        verifyProc.command = ["sh", "-c",
            'curl -sS -m 10 -o /dev/null -w "%{http_code}" ' +
            '-H "Authorization: Bearer ' + token + '" ' +
            service.apiBase + "/projects"
        ];
        verifyProc.running = false;
        verifyProc.running = true;
    }

    property string _pendingToken: ""

    property Process _writeProc: Process {
        id: writeProc
        onExited: (code) => {
            if (code === 0) {
                service.token = service._pendingToken;
                service._pendingToken = "";
                service.tokenSaved();
                service.verifyToken();
                service.syncNow();
            } else {
                service.lastError = "Could not write " + service.tokenPath;
                service.tokenRejected(service.lastError);
            }
        }
    }

    property Process _clearProc: Process {
        id: clearProc
        command: ["sh", "-c", "rm -f " + service.shellQuote(service.tokenPath)]
    }

    property Process _verifyProc: Process {
        id: verifyProc
        stdout: StdioCollector {
            onStreamFinished: {
                var code = parseInt(String(text).trim(), 10);
                service.verifying = false;

                if (code === 200) {
                    service.verified = true;
                    service.lastError = "";
                } else if (code === 401 || code === 403) {
                    service.verified = false;
                    service.lastError = "Todoist rejected the token.";
                    service.tokenRejected(service.lastError);
                } else if (code === 410) {
                    // The endpoint itself is gone -- a token change cannot fix
                    // this, so say so rather than blaming the network.
                    service.verified = false;
                    service.lastError = "This Todoist API version was retired; the shell needs updating.";
                } else {
                    // Offline or Todoist is down. The token may be perfectly
                    // fine, so it is kept -- work stays local until the next
                    // successful check.
                    service.verified = false;
                    service.lastError = "Could not reach Todoist (offline?).";
                }
            }
        }
        onExited: (code) => { if (code !== 0) service.verifying = false; }
    }

    // Load whatever is on disk at startup.
    property FileView _tokenFile: FileView {
        path: service.tokenPath
        printErrors: false

        onLoaded: {
            try {
                var parsed = JSON.parse(text());
                service.token = String((parsed && parsed.token) || "").trim();
                if (service.hasToken) service.verifyToken();
            } catch (e) {
                service.lastError = "Token file is not valid JSON.";
            }
        }
        // No file yet: the UI shows the connect prompt.
        onLoadFailed: (error) => service.token = ""
    }
}
