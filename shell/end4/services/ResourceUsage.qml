pragma Singleton
pragma ComponentBehavior: Bound

import qs.end4.modules.common
import QtQuick
import Quickshell
import Quickshell.Windows as Win

// Windows version of end-4's ResourceUsage (which reads /proc): the same properties from Quickshell for
// Windows' SystemStats (memory and swap in KB, CPU 0..1), polled at end-4's own interval.
Singleton {
    id: root
    property real memoryTotal: 1
    property real memoryFree: 0
    property real memoryUsed: memoryTotal - memoryFree
    property real memoryUsedPercentage: memoryUsed / memoryTotal
    property real swapTotal: 1
    property real swapFree: 0
    property real swapUsed: swapTotal - swapFree
    property real swapUsedPercentage: swapTotal > 0 ? (swapUsed / swapTotal) : 0
    property real cpuUsage: 0
    property var previousCpuStats

    property string maxAvailableMemoryString: kbToGbString(memoryTotal)
    property string maxAvailableSwapString: kbToGbString(swapTotal)
    property string maxAvailableCpuString: Win.SystemMonitor.cpuMaxMhz > 0 ? (Win.SystemMonitor.cpuMaxMhz / 1000).toFixed(1) + " GHz" : "--"

    readonly property int historyLength: Config?.options.resources.historyLength ?? 60
    property list<real> cpuUsageHistory: []
    property list<real> memoryUsageHistory: []
    property list<real> swapUsageHistory: []

    function kbToGbString(kb) {
        return (kb / (1024 * 1024)).toFixed(1) + " GB";
    }

    function _push(list, value) {
        const next = [...list, value];
        while (next.length > historyLength)
            next.shift();
        return next;
    }

    function updateHistories() {
        memoryUsageHistory = _push(memoryUsageHistory, memoryUsedPercentage);
        swapUsageHistory = _push(swapUsageHistory, swapUsedPercentage);
        cpuUsageHistory = _push(cpuUsageHistory, cpuUsage);
    }

    Component.onCompleted: {
        Win.SystemStats.active = true;
        Win.SystemMonitor.active = true;
    }

    Timer {
        interval: Config.options?.resources?.updateInterval ?? 3000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            const s = Win.SystemStats;
            root.memoryTotal = Math.max(1, Number(s.memoryTotalKb) || 1);
            root.memoryFree = Number(s.memoryAvailableKb) || 0;
            root.swapTotal = Number(s.swapTotalKb) || 0;
            root.swapFree = Number(s.swapAvailableKb) || 0;
            root.cpuUsage = Number(s.cpuUsage) || 0;
            root.updateHistories();
        }
    }
}
