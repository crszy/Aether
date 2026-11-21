pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import Caelestia.Config
import Caelestia.I18n
import qs.utils
import Quickshell.Windows as Win

Singleton {
    id: root

    property string osName
    property string osPrettyName
    property string osId
    property list<string> osIdLike
    property string osLogo: Qt.resolvedUrl(`${Quickshell.shellDir}/assets/logo.svg`)
    property bool isDefaultLogo: true

    property string uptime
    // Uptime trimmed to its two largest components
    property string uptimeShort
    readonly property string user: Quickshell.env("USERNAME") || Quickshell.env("USER") || ""
    readonly property string wm: "Quickshell (Windows)"
    readonly property string shell: "PowerShell"

    property string kernel
    property string hostname
    property string firmware

    // DMI vendor/model, combined into a single human-readable device name
    property string boardVendor
    property string boardName
    readonly property string device: {
        if (!boardName)
            return boardVendor;
        if (!boardVendor || boardName.toLowerCase().startsWith(boardVendor.toLowerCase()))
            return boardName;
        return `${boardVendor} ${boardName}`;
    }

    // Strips the placeholder strings OEMs commonly leave in DMI fields
    function sanitiseDmi(s: string): string {
        const t = s.trim();
        const junk = ["to be filled by o.e.m.", "system product name", "system manufacturer", "system version", "default string", "o.e.m.", "not specified", "not applicable", "unknown", "none", ""];
        return junk.includes(t.toLowerCase()) ? "" : t;
    }

    FileView {
        id: osRelease

        path: ""   // "/etc/os-release" does not exist on Windows
        onLoaded: {
            const lines = text().split("\n");

            const fd = key => lines.find(l => l.startsWith(`${key}=`))?.split("=")[1].replace(/"/g, "") ?? "";

            root.osName = fd("NAME");
            root.osPrettyName = fd("PRETTY_NAME");
            root.osId = fd("ID");
            root.osIdLike = fd("ID_LIKE").split(" ");

            const logo = Quickshell.iconPath(fd("LOGO"), true);
            if (GlobalConfig.general.logo === "caelestia") {
                root.osLogo = Qt.resolvedUrl(`${Quickshell.shellDir}/assets/logo.svg`);
                root.isDefaultLogo = true;
            } else if (GlobalConfig.general.logo) {
                root.osLogo = Quickshell.iconPath(GlobalConfig.general.logo, true) || "file://" + Paths.absolutePath(GlobalConfig.general.logo);
                root.isDefaultLogo = false;
            } else if (logo) {
                root.osLogo = logo;
                root.isDefaultLogo = false;
            }
        }
    }

    Connections {
        function onLogoChanged(): void {
            osRelease.reload();
        }

        target: GlobalConfig.general
    }

    FileView {
        path: ""   // "/proc/sys/kernel/osrelease" does not exist on Windows
        onLoaded: root.kernel = text().trim()
    }

    FileView {
        path: ""   // "/proc/sys/kernel/hostname" does not exist on Windows
        onLoaded: root.hostname = text().trim()
    }

    FileView {
        path: ""   // "/sys/class/dmi/id/sys_vendor" does not exist on Windows
        printErrors: false
        onLoaded: root.boardVendor = root.sanitiseDmi(text())
    }

    FileView {
        path: ""   // "/sys/class/dmi/id/product_name" does not exist on Windows
        printErrors: false
        onLoaded: root.boardName = root.sanitiseDmi(text())
    }

    FileView {
        path: ""   // "/sys/class/dmi/id/bios_version" does not exist on Windows
        printErrors: false
        onLoaded: root.firmware = root.sanitiseDmi(text())
    }

    Timer {
        running: true
        repeat: true
        interval: 15000
        onTriggered: root.updateUptime(Win.SystemMonitor.uptimeSeconds)
    }

    FileView {
        id: fileUptime

        path: ""   // "/proc/uptime" does not exist on Windows
        onLoaded: {
            const up = parseInt(text().split(" ")[0] ?? 0);

            const days = Math.floor(up / 86400);
            const hours = Math.floor((up % 86400) / 3600);
            const minutes = Math.floor((up % 3600) / 60);

            // TRANSLATORS: joins uptime components, e.g. "2 days, 3 hours"
            const sep = Tr.trCtx(", ", "uptime component separator");

            const parts = [];
            if (days > 0)
                parts.push(Tr.trN("%n day", "%n days", days));
            if (hours > 0)
                parts.push(Tr.trN("%n hour", "%n hours", hours));
            if (minutes > 0 || parts.length === 0)
                parts.push(Tr.trN("%n minute", "%n minutes", minutes));

            root.uptime = parts.join(sep);
            root.uptimeShort = parts.slice(0, 2).join(sep);
        }
    }

    // ---- Windows ----
    function updateUptime(up: int): void {
        const days = Math.floor(up / 86400), hours = Math.floor((up % 86400) / 3600), minutes = Math.floor((up % 3600) / 60);
        const sep = Tr.trCtx(", ", "uptime component separator");
        const parts = [];
        if (days > 0) parts.push(Tr.trN("%n day", "%n days", days));
        if (hours > 0) parts.push(Tr.trN("%n hour", "%n hours", hours));
        if (minutes > 0 || parts.length === 0) parts.push(Tr.trN("%n minute", "%n minutes", minutes));
        root.uptime = parts.join(sep);
        root.uptimeShort = parts.slice(0, 2).join(sep);
    }
    Connections {
        target: Win.SystemMonitor
        function onUptimeSecondsChanged(): void { root.updateUptime(Win.SystemMonitor.uptimeSeconds); }
    }
    Component.onCompleted: {
        const os = Win.Session.osInfo();
        root.osName = os.name ?? "Windows";
        root.osPrettyName = `${os.name ?? "Windows"} ${os.version ?? ""}`.trim();
        root.osId = "windows";
        root.osIdLike = [];
        root.kernel = os.build ?? "";
        root.hostname = Quickshell.env("COMPUTERNAME") || "";
        // the port's SystemMonitor only samples while active; uptime needs nothing faster than this
        if (Win.SystemMonitor.intervalMs < 30000)
            Win.SystemMonitor.intervalMs = 30000;
        Win.SystemMonitor.active = true;
        root.updateUptime(Win.SystemMonitor.uptimeSeconds);
    }
}