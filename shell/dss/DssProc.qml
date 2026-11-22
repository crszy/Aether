// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows stand-in for Quickshell.Io's Process, added by dotfiles/dss-port.js: the command (bash text or a
// program) is run by DssWin's interpreter. A finite command feeds stdout and ends; a stream (cava, niri's event
// stream, python helpers) stays "running" with no output, so nothing restarts it in a loop.
import QtQuick

QtObject {
    id: p

    property var command: []
    property bool running: false
    property QtObject stdout: null
    property QtObject stderr: null
    property var environment: ({})
    property string workingDirectory: ""
    property bool clearEnvironment: false
    property bool stdinEnabled: false
    readonly property int processId: 0
    default property list<QtObject> kids

    signal started
    signal exited(int exitCode, int exitStatus)

    function feed(parser: QtObject, text: string): void {
        if (!parser)
            return;
        if (parser.splitMarker !== undefined) {
            const parts = text.split(parser.splitMarker || "\n");
            if (parts.length && parts[parts.length - 1] === "")
                parts.pop();
            for (let i = 0; i < parts.length; i++)
                parser.read(parts[i]);
        } else if (parser.text !== undefined) {
            parser.text = text;
            parser.streamFinished();
        }
    }

    function go(): void {
        if (!p.running)
            return;
        const r = DssWin.exec(p.command);
        p.started();
        if (r.stream)
            return;
        p.feed(p.stdout, r.out);
        p.feed(p.stderr, "");
        p.running = false;
        p.exited(r.status, 0);
    }

    function startDetached(): void {
        DssWin.run(p.command);
    }
    function exec(cmd: var): void {
        p.command = cmd;
        p.running = false;
        p.running = true;
    }
    function signal(sig: int): void {
        p.running = false;
    }
    function write(data: string): void {
    }

    onRunningChanged: if (running)
        Qt.callLater(go)
    // "running: true" written in the file is no change, so it would never start without this
    Component.onCompleted: if (running)
        Qt.callLater(go)
}
