pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as Win
import qs.services as Cae
import qs.utils as CaeU

// GNOME 48 Digital Wellbeing: screen time (minutes you actually used the PC: input in the last 2 minutes), break
// reminders and a daily limit. Counted once a minute while the Debian look is on; the last 7 days are kept in
// ~/.local/state/caelestia/distro-wellbeing.json.
Singleton {
    id: root

    readonly property string today: Qt.formatDate(Cae.Time.date, "yyyy-MM-dd")
    readonly property int todayMinutes: days[today] ?? 0
    property var days: ({})
    property int sinceBreak: 0        // minutes used since the last 5-minute pause
    property bool limitWarned: false
    property bool ready: false

    // the last 7 days, oldest first: [label, minutes]
    function week(): var {
        const out = [];
        const d = new Date(Cae.Time.date);
        d.setDate(d.getDate() - 6);
        for (let i = 0; i < 7; i++) {
            out.push([Qt.formatDate(d, "ddd"), days[Qt.formatDate(d, "yyyy-MM-dd")] ?? 0]);
            d.setDate(d.getDate() + 1);
        }
        return out;
    }
    function format(mins: int): string {
        const h = Math.floor(mins / 60);
        return h > 0 ? `${h} h ${mins % 60} min` : `${mins} min`;
    }
    function notify(title: string, body: string): void {
        Cae.Notifs.notifySend(title, body, ["-a", "Digital Wellbeing", "-i", "hourglass"]);
    }

    FileView {
        id: file
        path: `${CaeU.Paths.state}/distro-wellbeing.json`
        watchChanges: false
        onLoaded: {
            try {
                root.days = JSON.parse(text()).days ?? {};
            } catch (e) {
                root.days = {};
            }
            root.ready = true;
        }
        onLoadFailed: root.ready = true
    }

    Timer {
        interval: 60000
        repeat: true
        running: root.ready && Cae.WinSettings.distroMode
        onTriggered: {
            const idle = Win.IdlePower.idleSeconds();
            if (idle >= 300)
                root.sinceBreak = 0;   // a real pause counts as a break
            if (idle >= 120)
                return;

            const days = Object.assign({}, root.days);
            days[root.today] = (days[root.today] ?? 0) + 1;
            const keep = Object.keys(days).sort().slice(-7);
            root.days = keep.reduce((o, k) => { o[k] = days[k]; return o; }, {}); // no Object.fromEntries in this JS engine
            file.setText(JSON.stringify({ days: root.days }));

            root.sinceBreak++;
            if (Cae.WinSettings.distroBreaks && root.sinceBreak >= Cae.WinSettings.distroBreakEvery) {
                root.sinceBreak = 0;
                root.notify("Time for a break", `You've been using the computer for ${Cae.WinSettings.distroBreakEvery} minutes. Look away from the screen and stretch.`);
            }
            const limit = Cae.WinSettings.distroScreenLimit;
            if (limit > 0 && root.todayMinutes >= limit && !root.limitWarned) {
                root.limitWarned = true;
                root.notify("Screen time limit reached", `You've used the computer for ${root.format(root.todayMinutes)} today.`);
            }
        }
    }
    onTodayChanged: limitWarned = false
}
