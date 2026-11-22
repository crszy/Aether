pragma Singleton
pragma ComponentBehavior: Bound

import qs.end4.modules.common
import qs.end4.modules.common.functions
import QtQuick
import Quickshell.Windows as WinClip
import Quickshell
import Quickshell.Io

Singleton {
    id: root
    // property string cliphistBinary: FileUtils.trimFileProtocol(`${Directories.home}/.cargo/bin/stash`)
    property string cliphistBinary: "cliphist"
    property real pasteDelay: 0.05
    property string pressPasteCommand: "ydotool key -d 1 29:1 47:1 47:0 29:0"
    property bool sloppySearch: Config.options?.search.sloppy ?? false
    property real scoreThreshold: 0.2
    property list<string> entries: WinClip.Clipboard.entries // Windows
    readonly property var preparedEntries: entries.map(a => ({
        name: Fuzzy.prepare(`${a.replace(/^\s*\S+\s+/, "")}`),
        entry: a
    }))
    function fuzzyQuery(search: string): var {
        if (search.trim() === "") {
            return entries;
        }
        if (root.sloppySearch) {
            const results = entries.slice(0, 100).map(str => ({
                entry: str,
                score: Levendist.computeTextMatchScore(str.toLowerCase(), search.toLowerCase())
            })).filter(item => item.score > root.scoreThreshold)
                .sort((a, b) => b.score - a.score)
            return results
                .map(item => item.entry)
        }

        return Fuzzy.go(search, preparedEntries, {
            all: true,
            key: "name"
        }).map(r => {
            return r.obj.entry
        });
    }

    function entryIsImage(entry) {
        return !!(/^\d+\t\[\[.*binary data.*\d+x\d+.*\]\]$/.test(entry))
    }

    function refresh() {} // Windows: entries follow the clipboard

    function copy(entry) { WinClip.Clipboard.copy(parseInt(entry.split("\t")[0])); }

    function paste(entry) { WinClip.Clipboard.copy(parseInt(entry.split("\t")[0])); }

    function superpaste(count, isImage = false) {} // Windows: not supported

    Process {
        id: deleteProc
        property string entry: ""
        command: ["cmd.exe", "/c", "exit 0"] // Windows: unused
        function deleteEntry(entry) {
            deleteProc.entry = entry;
            deleteProc.running = true;
            deleteProc.entry = "";
        }
        onExited: (exitCode, exitStatus) => {
            root.refresh();
        }
    }

    function deleteEntry(entry) { WinClip.Clipboard.deleteEntry(parseInt(entry.split("\t")[0])); }

    Process {
        id: wipeProc
        command: ["cmd.exe", "/c", "exit 0"] // Windows: unused
        onExited: (exitCode, exitStatus) => {
            root.refresh();
        }
    }

    function wipe() { WinClip.Clipboard.wipe(); }

    Connections {
        target: Quickshell
        function onClipboardTextChanged() {
            delayedUpdateTimer.restart()
        }
    }

    Timer {
        id: delayedUpdateTimer
        interval: Config.options.hacks.arbitraryRaceConditionDelay
        repeat: false
        onTriggered: {
            root.refresh()
        }
    }

    Process {
        id: readProc
        property list<string> buffer: []

        command: ["cmd.exe", "/c", "exit 0"] // Windows: unused

        stdout: SplitParser {
            onRead: (line) => {
                readProc.buffer.push(line)
            }
        }

        onExited: (exitCode, exitStatus) => {
            if (exitCode === 0) {
                root.entries = readProc.buffer
            } else {
                console.error("[Cliphist] Failed to refresh with code", exitCode, "and status", exitStatus)
            }
        }
    }

    IpcHandler {
        target: "end4:cliphistService"

        function update(): void {
            root.refresh()
        }
    }
}
