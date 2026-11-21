// services/SystemServicesService.qml
//
// Background service managing system & user daemons via scripts/services_manager.py.
// Polls status when the Control Center is visible, executes start/stop/restart/toggleEnable,
// and streams filtered journalctl logs for any service.

pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io
import "../Settings"

QtObject {
    id: root

    property var services: []
    property int totalCount: 0
    property int runningCount: 0
    property int stoppedCount: 0
    property int failedCount: 0
    property string totalMemoryFormatted: "0 B"
    property bool refreshing: false
    property string actionInProgress: ""

    // Log viewer state
    property string activeLogUnit: ""
    property string activeLogScope: "system"
    property var logEntries: []
    property string logRawText: ""
    property bool logsLoading: false
    property string lastActionMessage: ""

    readonly property string scriptPath: PathSettings.scriptsDir + "/services_manager.py"

    // Timer only ticks while the Control Center dashboard is open
    property Timer autoRefreshTimer: Timer {
        interval: 8000
        repeat: true
        running: CenterState.qsVisible && (CenterState.activeTab === "Pomodoro" || CenterState.activeTab === "Default")
        onTriggered: root.refresh()
    }

    Component.onCompleted: {
        root.refresh();
    }

    function refresh() {
        if (listProcess.running) return;
        refreshing = true;
        listProcess.running = true;
    }

    function startService(scope, unit) {
        actionInProgress = unit + ":start";
        execAction("start", scope, unit);
    }

    function stopService(scope, unit) {
        actionInProgress = unit + ":stop";
        execAction("stop", scope, unit);
    }

    function restartService(scope, unit) {
        actionInProgress = unit + ":restart";
        execAction("restart", scope, unit);
    }

    function toggleEnable(scope, unit, currentState) {
        let action = currentState ? "disable" : "enable";
        actionInProgress = unit + ":" + action;
        execAction(action, scope, unit);
    }

    function fetchLogs(scope, unit, lines) {
        activeLogScope = scope || "system";
        activeLogUnit = unit;
        logsLoading = true;
        let lineCount = lines || 40;
        logsProcess.command = ["python3", scriptPath, "logs", activeLogScope, activeLogUnit, lineCount.toString()];
        logsProcess.running = false;
        logsProcess.running = true;
    }

    function clearLogs() {
        activeLogUnit = "";
        logEntries = [];
        logRawText = "";
        logsLoading = false;
    }

    function execAction(action, scope, unit) {
        actionProcess.command = ["python3", scriptPath, action, scope, unit];
        actionProcess.running = false;
        actionProcess.running = true;
    }

    // --- PROCESSES ---

    property Process listProcess: Process {
        command: ["python3", scriptPath, "list"]
        stdout: StdioCollector {
            onStreamFinished: {
                root.refreshing = false;
                try {
                    let data = JSON.parse(String(text).trim());
                    if (data && data.services) {
                        root.services = data.services;
                        if (data.stats) {
                            root.totalCount = data.stats.total || 0;
                            root.runningCount = data.stats.running || 0;
                            root.stoppedCount = data.stats.stopped || 0;
                            root.failedCount = data.stats.failed || 0;
                            root.totalMemoryFormatted = data.stats.totalMemoryFormatted || "0 B";
                        }
                    }
                } catch (e) {
                    console.log("Services list parse error:", e);
                }
            }
        }
    }

    property Process actionProcess: Process {
        stdout: StdioCollector {
            onStreamFinished: {
                root.actionInProgress = "";
                try {
                    let res = JSON.parse(String(text).trim());
                    if (res && res.message) {
                        root.lastActionMessage = res.message;
                    }
                } catch (e) {}
                // Refresh list after action completes
                root.refresh();
                if (root.activeLogUnit !== "") {
                    root.fetchLogs(root.activeLogScope, root.activeLogUnit);
                }
            }
        }
    }

    property Process logsProcess: Process {
        stdout: StdioCollector {
            onStreamFinished: {
                root.logsLoading = false;
                try {
                    let data = JSON.parse(String(text).trim());
                    if (data && data.logs) {
                        root.logEntries = data.logs;
                        root.logRawText = data.rawText || "";
                    }
                } catch (e) {
                    root.logEntries = [{
                        raw: "Error parsing logs: " + e,
                        timestamp: "",
                        message: "Error parsing logs",
                        level: "error"
                    }];
                }
            }
        }
    }
}
