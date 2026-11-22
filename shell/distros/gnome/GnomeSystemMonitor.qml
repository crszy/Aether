import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Windows as Win

// GNOME System Monitor (the task viewer): a libadwaita window with Processes and Resources tabs, real Windows
// processes (CPU %, memory, ID), search, End Process. Ctrl+Shift+Esc opens it in Debian mode.
FloatingWindow {
    id: win

    signal dismissed

    property string tab: "processes"
    property string filter: ""
    property string sortKey: "cpu"
    property bool sortDown: true
    property int selectedPid: -1

    property var rows: []
    property var lastTimes: ({})
    property real lastWall: 0
    property var cpuHistory: []
    property var memHistory: []
    property var lastSys: null
    property var sys: ({})

    title: "System Monitor"
    implicitWidth: 900
    implicitHeight: 640
    color: "#1e1e1e"
    onClosed: dismissed()   // the window's own close button

    function fmtBytes(b: real): string {
        if (b >= 1073741824) return (b / 1073741824).toFixed(1) + " GB";
        if (b >= 1048576) return (b / 1048576).toFixed(1) + " MB";
        if (b >= 1024) return (b / 1024).toFixed(0) + " kB";
        return b + " bytes";
    }

    function refresh(): void {
        const now = Date.now();
        const s = Win.FsUtils.systemStats();
        const cpus = s.cpus || 1;
        const list = Win.FsUtils.processes();
        const times = {};
        const wall = (now - lastWall) * 10000; // ms -> 100 ns
        const out = [];
        for (const p of list) {
            if (p.pid === 0)
                continue;
            const prev = lastTimes[p.pid];
            times[p.pid] = p.cpuTime;
            const cpu = prev !== undefined && lastWall > 0 && wall > 0 ? Math.max(0, (p.cpuTime - prev) / (wall * cpus) * 100) : 0;
            out.push({ pid: p.pid, name: p.name.replace(/\.exe$/i, ""), cpu, memory: p.memory, threads: p.threads, path: p.path });
        }
        lastTimes = times;
        lastWall = now;
        rows = out;

        if (lastSys) {
            const idle = s.idle - lastSys.idle;
            const total = (s.kernel - lastSys.kernel) + (s.user - lastSys.user);
            const used = total > 0 ? Math.max(0, Math.min(1, 1 - idle / total)) : 0;
            cpuHistory = cpuHistory.concat([used]).slice(-60);
        }
        lastSys = s;
        sys = s;
        memHistory = memHistory.concat([s.total ? 1 - s.available / s.total : 0]).slice(-60);
    }

    readonly property var shown: {
        const q = filter.toLowerCase();
        const r = rows.filter(p => !q || p.name.toLowerCase().includes(q) || String(p.pid).includes(q));
        const k = sortKey;
        r.sort((a, b) => {
            const d = k === "name" ? a.name.localeCompare(b.name) : (a[k] - b[k]);
            return sortDown ? -d : d;
        });
        return r;
    }

    Timer {
        interval: 2000
        running: win.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: win.refresh()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- header bar: search toggle, view switcher, End Process
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 48
            color: "#303030"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6
                HeaderButton { glyph: "search"; on: searchBox.visible; onClicked: { searchBox.visible = !searchBox.visible; if (!searchBox.visible) win.filter = ""; else searchInput.forceActiveFocus(); } }
                Item { Layout.fillWidth: true }
                Row {
                    spacing: 4
                    Repeater {
                        model: [["processes", "Processes", "view_list"], ["resources", "Resources", "monitoring"]]
                        Rectangle {
                            required property var modelData
                            width: tabRow.implicitWidth + 28
                            height: 34
                            radius: 8
                            color: win.tab === modelData[0] ? Qt.rgba(1, 1, 1, 0.12) : tabArea.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                            Row {
                                id: tabRow
                                anchors.centerIn: parent
                                spacing: 6
                                Text { text: modelData[2]; font.family: Adw.icons; font.pixelSize: 17; color: Adw.text; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: modelData[1]; font.family: Adw.font; font.pixelSize: 14; font.weight: Font.Bold; color: Adw.text; anchors.verticalCenter: parent.verticalCenter }
                            }
                            MouseArea { id: tabArea; anchors.fill: parent; hoverEnabled: true; onClicked: win.tab = modelData[0] }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    visible: win.tab === "processes"
                    implicitWidth: endText.implicitWidth + 24
                    implicitHeight: 34
                    radius: 8
                    opacity: win.selectedPid > 0 ? 1 : 0.4
                    color: endArea.containsMouse && win.selectedPid > 0 ? "#e62d42" : "#c01c28"
                    Text { id: endText; anchors.centerIn: parent; text: "End Process"; color: "#ffffff"; font.family: Adw.font; font.pixelSize: 14; font.weight: Font.Bold }
                    MouseArea {
                        id: endArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: if (win.selectedPid > 0 && Win.FsUtils.endProcess(win.selectedPid)) { win.selectedPid = -1; win.refresh(); }
                    }
                }
            }
        }

        // ---- search bar
        Rectangle {
            id: searchBox
            visible: false
            Layout.fillWidth: true
            implicitHeight: 50
            color: "#2a2a2a"
            Rectangle {
                anchors.centerIn: parent
                width: 360
                height: 34
                radius: 8
                color: Qt.rgba(1, 1, 1, 0.08)
                TextInput {
                    id: searchInput
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: Text.AlignVCenter
                    color: Adw.text
                    font.family: Adw.font
                    font.pixelSize: 14
                    onTextChanged: win.filter = text
                    Keys.onEscapePressed: { text = ""; searchBox.visible = false; }
                }
            }
        }

        // ---- processes
        ColumnLayout {
            visible: win.tab === "processes"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {   // column headers
                Layout.fillWidth: true
                implicitHeight: 32
                color: "#242424"
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    ColumnHead { label: "Process Name"; key: "name"; width: parent.width - 16 - 3 * 120 }
                    ColumnHead { label: "% CPU"; key: "cpu"; width: 120 }
                    ColumnHead { label: "Memory"; key: "memory"; width: 120 }
                    ColumnHead { label: "ID"; key: "pid"; width: 120 }
                }
            }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: win.shown
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: list.width
                    height: 34
                    color: win.selectedPid === modelData.pid ? Qt.alpha(Adw.accent, 0.45) : rowArea.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : index % 2 ? "#232323" : "#1e1e1e"
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        Cell { text: modelData.name; width: parent.width - 16 - 3 * 120; bold: true }
                        Cell { text: modelData.cpu.toFixed(1); width: 120 }
                        Cell { text: modelData.memory > 0 ? win.fmtBytes(modelData.memory) : "N/A"; width: 120 }
                        Cell { text: modelData.pid; width: 120 }
                    }
                    MouseArea {
                        id: rowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: win.selectedPid = modelData.pid
                    }
                }
            }
        }

        // ---- resources: CPU and memory history
        ColumnLayout {
            visible: win.tab === "resources"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            spacing: 14

            Text { text: "CPU"; color: Adw.text; font.family: Adw.font; font.pixelSize: 16; font.weight: Font.Bold }
            Graph { Layout.fillWidth: true; Layout.fillHeight: true; values: win.cpuHistory; line: Adw.accent }
            Text {
                text: `${((win.cpuHistory[win.cpuHistory.length - 1] ?? 0) * 100).toFixed(1)}% of ${win.sys.cpus ?? "?"} CPUs`
                color: Adw.dim; font.family: Adw.font; font.pixelSize: 13
            }
            Text { text: "Memory"; color: Adw.text; font.family: Adw.font; font.pixelSize: 16; font.weight: Font.Bold; Layout.topMargin: 10 }
            Graph { Layout.fillWidth: true; Layout.fillHeight: true; values: win.memHistory; line: "#c061cb" }
            Text {
                text: win.sys.total ? `${win.fmtBytes(win.sys.total - win.sys.available)} (${((1 - win.sys.available / win.sys.total) * 100).toFixed(1)}%) of ${win.fmtBytes(win.sys.total)}` : ""
                color: Adw.dim; font.family: Adw.font; font.pixelSize: 13
            }
        }
    }

    component HeaderButton: Rectangle {
        property string glyph
        property bool on
        signal clicked
        implicitWidth: 36
        implicitHeight: 34
        radius: 8
        color: on ? Qt.rgba(1, 1, 1, 0.15) : hbArea.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        Text { anchors.centerIn: parent; text: parent.glyph; font.family: Adw.icons; font.pixelSize: 18; color: Adw.text }
        MouseArea { id: hbArea; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
    }

    component ColumnHead: Item {
        property string label
        property string key
        height: parent.height
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: parent.label + (win.sortKey === parent.key ? (win.sortDown ? "  ▾" : "  ▴") : "")
            color: Adw.dim
            font.family: Adw.font
            font.pixelSize: 13
            font.weight: Font.Bold
        }
        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (win.sortKey === parent.key) win.sortDown = !win.sortDown;
                else { win.sortKey = parent.key; win.sortDown = parent.key !== "name"; }
            }
        }
    }

    component Cell: Text {
        property bool bold
        height: parent.height
        verticalAlignment: Text.AlignVCenter
        color: Adw.text
        font.family: Adw.font
        font.pixelSize: 13
        elide: Text.ElideRight
        rightPadding: 10
    }

    component Graph: Rectangle {
        id: graph
        property var values: []
        property color line
        color: "#242424"
        radius: 8
        onValuesChanged: canvas.requestPaint()
        Canvas {
            id: canvas
            anchors.fill: parent
            anchors.margins: 8
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.08);
                for (let i = 1; i < 4; i++) {
                    ctx.beginPath();
                    ctx.moveTo(0, height * i / 4);
                    ctx.lineTo(width, height * i / 4);
                    ctx.stroke();
                }
                const v = graph.values;
                if (v.length < 2) return;
                const step = width / 59;
                const x0 = width - (v.length - 1) * step;
                ctx.beginPath();
                ctx.moveTo(x0, height);
                for (let i = 0; i < v.length; i++) ctx.lineTo(x0 + i * step, height - v[i] * height);
                ctx.lineTo(width, height);
                ctx.closePath();
                ctx.fillStyle = Qt.alpha(graph.line, 0.25);
                ctx.fill();
                ctx.beginPath();
                for (let i = 0; i < v.length; i++) {
                    const x = x0 + i * step, y = height - v[i] * height;
                    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
                }
                ctx.strokeStyle = graph.line;
                ctx.lineWidth = 2;
                ctx.stroke();
            }
        }
    }
}
