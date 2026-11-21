import QtQuick
import Quickshell
import Quickshell.Windows as Win
import "../Settings"

pragma Singleton

// Windows version of Zenith's ResourceService (the original streams scripts/resources.sh: /proc, sensors).
// Same properties, read from Quickshell.Windows' SystemStats (CPU, memory, network, every 1.5 s) and
// SystemMonitor (temperature, disks, CPU name - it refreshes slowly, which is fine for those).
Item {
    id: service

    readonly property int cpu: Math.round(Win.SystemStats.cpuUsage * 100)
    readonly property int mem: Win.SystemStats.memoryTotalKb > 0
        ? Math.round((1 - Win.SystemStats.memoryAvailableKb / Win.SystemStats.memoryTotalKb) * 100) : 0
    readonly property int temp: Math.round(Win.SystemMonitor.cpuTemperature || 0)
    readonly property double load: Win.SystemStats.cpuUsage * Math.max(1, Win.SystemMonitor.cpuThreads)
    readonly property int loadPerc: cpu
    readonly property int fs: {
        for (const d of Win.SystemMonitor.disks || [])
            for (const v of d.volumes || [])
                if (/^C:/i.test(v.mount) && v.totalBytes > 0)
                    return Math.round(v.usedBytes / v.totalBytes * 100);
        return 0;
    }
    readonly property string cpuModel: Win.SystemStats.cpuName || Win.SystemMonitor.cpuName
    readonly property string freq: Win.SystemMonitor.cpuMhz > 0 ? (Win.SystemMonitor.cpuMhz / 1000).toFixed(2) + " GHz" : ""
    readonly property string arch: "x86_64"
    readonly property string kernel: Win.SystemMonitor.osVersion ? "Windows " + Win.SystemMonitor.osVersion : "Windows"
    readonly property string ip: WifiService.ipv4Address
    // Throughput, KB/s
    readonly property string netIface: Win.Network.activeSsid !== "" ? "Wi-Fi" : (Win.Network.ethernetConnected ? "Ethernet" : "")
    readonly property int netDown: Math.round(Win.SystemStats.networkDownloadBytesPerSec / 1024)
    readonly property int netUp: Math.round(Win.SystemStats.networkUploadBytesPerSec / 1024)

    // only ever switched on: Caelestia and Noctalia share these backends
    Component.onCompleted: {
        Win.SystemStats.active = true;
        Win.SystemStats.networkEnabled = true;
        Win.SystemMonitor.active = true;
    }
}
