import ".."
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Services.Notifications
import "../windows" as Win

pragma Singleton

Item {
    id: root

    property alias notifications: historyModel
    property string lastNotifKey: ""

    signal notificationReceived(var notifData)
    signal notificationDismissed(real id)
    signal osdReceived(string type, real value)

    function updateOSDValue(type, value) {
        let percent = Math.round(value * 100);
        if (type === "volume")
            shellExec.command = ["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", percent + "%"];
        else if (type === "brightness")
            shellExec.command = ["brightnessctl", "set", percent + "%"];
        shellExec.running = false;
        shellExec.running = true;
    }

    function clearAll() {
        for (let i = 0; i < historyModel.count; i++) {
            let n = historyModel.get(i);
            if (n && n.originalNotif)
                n.originalNotif.dismiss();
        }
        historyModel.clear();
    }

    function removeNotification(notifId) {
        for (let i = 0; i < historyModel.count; i++) {
            if (historyModel.get(i).id === notifId) {
                historyModel.remove(i);
                break;
            }
        }
        root.notificationDismissed(notifId);
    }

    function dismissNotification(notifId) {
        root.notificationDismissed(notifId);
    }

    Timer {
        id: duplicateResetTimer
        interval: 5000
        onTriggered: root.lastNotifKey = ""
    }

    ListModel {
        id: historyModel
    }

    Process {
        id: shellExec
    }

    NotificationServer {
        id: server
        imageSupported: true

        onNotification: (notif) => {
            // OSD Filtering
            let syncHint = notif.hints["x-canonical-private-synchronous"] || "";
            let category = notif.hints["category"] || notif.category || "";
            
            if (syncHint === "volume" || syncHint === "brightness" || category === "volume" || category === "brightness") {
                let type = (syncHint === "volume" || category === "volume") ? "volume" : "brightness";
                let text = (notif.summary || "") + " " + (notif.body || "");
                let match = text.match(/(\d+)%/);
                let isMuted = text.toLowerCase().includes("muted") || text.toLowerCase().includes("mute");
                if (match || isMuted) {
                    let val = isMuted ? 0 : (parseInt(match[1]) / 100);
                    root.osdReceived(type, val);
                    notif.dismiss();
                    return;
                }
            }

            // Duplicate Filtering
            let isBattery = (notif.appName === "Battery");
            let isCaffeine = (notif.appName === "Caffeine");
            if (!isBattery && !isCaffeine) {
                let currentKey = notif.summary + "|" + notif.body + "|" + notif.appName;
                for (let i = 0; i < historyModel.count; i++) {
                    let item = historyModel.get(i);
                    if (item.summary === notif.summary && item.body === notif.body && item.appName === notif.appName) {
                        notif.dismiss();
                        return;
                    }
                }
                root.lastNotifKey = currentKey;
                duplicateResetTimer.restart();
            }

            // Dynamic Icon Candidate Resolution via IconsFetcher
            let rawIcon = notif.appIcon || "";
            let rawImg = notif.image || "";
            let candidates = [];
            for (const p of [rawImg, rawIcon]) // Windows
                if (/^[A-Za-z]:[\\/]/.test(p)) candidates.push("file:///" + p.split("\\").join("/"));

            if (rawImg !== "") {
                candidates.push(rawImg.startsWith("file:///") || rawImg.startsWith("/") ? (rawImg.startsWith("file:///") ? rawImg : "file:///" + rawImg) : rawImg);
            }

            let fetcherCandidates = Win.IconsFetcher.getIconCandidates(notif.appName || "", notif.desktopEntry || "", rawIcon);
            for (let cand of fetcherCandidates) {
                candidates.push(cand);
            }

            let validCandidates = candidates.filter((v, i, a) => v && v !== "" && a.indexOf(v) === i);

            let notifData = {
                "id": Date.now() + Math.random(),
                "summary": notif.summary || "",
                "body": notif.body || "",
                "appIcon": validCandidates.length > 0 ? validCandidates[0] : "",
                "iconCandidates": validCandidates,
                "rawIcon": rawIcon,
                "appName": notif.appName || "System",
                "desktopEntry": notif.desktopEntry || "",
                "originalNotif": notif
            };

            if (!isBattery && !isCaffeine) {
                historyModel.insert(0, notifData);
            }
            if (!PowerToolsService.dnd) {
                root.notificationReceived(notifData);
            }
        }
    }
}