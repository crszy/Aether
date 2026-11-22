pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Windows as Win
import qs.dms.Services

// Windows stand-in for DMS's "dgop" system monitor (a Go program DMS asks through its backend socket).
// DgopService sends its dgop.* requests here (dms-port.js); the answers come in dgop's own format from
// Quickshell for Windows' SystemStats (CPU, memory, network) and SystemMonitor (temperature, disks, OS).
// Everything else still goes to DMSService. Nothing here scans Wi-Fi or starts processes.
Singleton {
    id: root

    // the samplers only run while a DMS widget shows them (DgopService counts its users): idle otherwise
    readonly property bool _wanted: DgopService.pollingActive
    readonly property var _mods: DgopService.enabledModules ?? []
    Binding { target: Win.SystemStats; property: "active"; value: root._wanted }
    Binding { target: Win.SystemStats; property: "networkEnabled"; value: root._wanted && (root._mods.includes("network") || root._mods.includes("all")) }
    Binding { target: Win.SystemMonitor; property: "active"; value: root._wanted && root._mods.some(m => ["hardware", "diskmounts", "system", "all"].includes(m)) }

    readonly property var modules: ["cpu", "memory", "network", "diskmounts", "system", "hardware"]

    function handle(method, params, callback) {
        if (!String(method).startsWith("dgop.")) {
            DMSService.sendRequest(method, params, callback);
            return;
        }
        let result = {};
        if (method === "dgop.modules")
            result = { available: root.modules };
        else if (method === "dgop.meta")
            result = meta(params?.modules ?? ["all"]);
        else if (method === "dgop.gpu")
            result = { gpus: [] };
        if (callback)
            Qt.callLater(() => callback({ result: result }));
    }

    function _size(bytes) {
        const units = ["B", "K", "M", "G", "T"];
        let v = Number(bytes) || 0, i = 0;
        while (v >= 1024 && i < units.length - 1) {
            v /= 1024;
            i++;
        }
        return (v >= 10 || i === 0 ? Math.round(v) : v.toFixed(1)) + units[i];
    }

    function _bootTime() {
        const up = Number(Win.SystemMonitor.uptimeSeconds || Win.SystemStats.uptimeSeconds || 0);
        if (!up)
            return "";
        const d = new Date(Date.now() - up * 1000);
        const p = n => String(n).padStart(2, "0");
        return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
    }

    function mounts() {
        const out = [];
        for (const disk of (Win.SystemMonitor.disks ?? []))
            for (const v of (disk.volumes ?? [])) {
                const total = Number(v.totalBytes) || 0;
                const used = Number(v.usedBytes) || 0;
                if (!total)
                    continue;
                out.push({
                    device: v.mount,
                    mount: String(v.mount).replace(/\\$/, ""),
                    fstype: v.fileSystem ?? "NTFS",
                    size: _size(total),
                    used: _size(used),
                    avail: _size(total - used),
                    percent: Math.round(used / total * 100) + "%"
                });
            }
        return out;
    }

    function meta(wanted) {
        const all = wanted.includes("all");
        const want = m => all || wanted.includes(m);
        const s = Win.SystemStats;
        const m = Win.SystemMonitor;
        const data = {};
        if (want("cpu"))
            data.cpu = {
                usage: (Number(s.cpuUsage) || 0) * 100,
                frequency: Number(m.cpuMhz) || 0,
                temperature: Number(m.cpuTemperature) || 0,
                count: Number(m.cpuThreads) || 1,
                model: m.cpuName || s.cpuName || "",
                coreUsage: []
            };
        if (want("memory")) {
            const total = Number(s.memoryTotalKb) || 0;
            const avail = Number(s.memoryAvailableKb) || 0;
            data.memory = {
                total: total,
                available: avail,
                free: avail,
                used: total - avail,
                swaptotal: Number(s.swapTotalKb) || 0,
                swapfree: Number(s.swapAvailableKb) || 0
            };
        }
        if (want("network"))
            data.network = [{
                    name: "Windows",
                    rx: Number(s.networkDownloadedBytes) || 0,
                    tx: Number(s.networkUploadedBytes) || 0
                }];
        if (want("diskmounts"))
            data.diskmounts = mounts();
        if (want("system") || want("hardware"))
            data.system = { boottime: _bootTime(), loadavg: "" };
        if (want("hardware"))
            data.hardware = {
                hostname: m.hostName || "",
                kernel: m.osBuild ? "NT " + m.osBuild : "",
                distro: [m.osName, m.osVersion].filter(x => !!x).join(" "),
                arch: "x86_64",
                bios: { motherboard: [m.boardVendor, m.boardModel].filter(x => !!x).join(" "), version: "" }
            };
        return data;
    }
}
