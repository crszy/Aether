// Aether's own security key: any USB stick (or SD card) registered as a key unlocks the lock screens while it is
// plugged in. Registering writes a random 256-bit token to <drive>\.aether-key; only a salted PBKDF2 hash of it is
// kept on the PC (%LOCALAPPDATA%\AetherShell\security-key.json), so the settings file alone unlocks nothing.
// Used by the Pam stand-in (PamContext "u2f" configs: DMS, Zenith...) and Ryoku's qylock. Lock screens set
// `watching` while they are up; the drives are looked at once a second only then.
pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as Win

Singleton {
    id: root

    readonly property string store: `${String(Quickshell.env("LOCALAPPDATA") || "").replace(/\\/g, "/")}/AetherShell/security-key.json`
    readonly property bool registered: data.hash !== "" && data.salt !== ""
    readonly property string label: data.label
    property bool watching: false
    property bool present: false
    signal found()

    // drive letters that exist now, C: left out (the system drive is never a key)
    function drives(): var {
        const out = [];
        for (let c = 68; c <= 90; c++) {   // D..Z
            const root = String.fromCharCode(c) + ":/";
            if (Win.FsUtils.classify(root) === "dir")
                out.push(root);
        }
        return out;
    }

    // reads a small text file (FileView: works without QML_XHR_ALLOW_FILE_READ)
    function readText(path: string): string {
        reader.path = "";
        reader.path = path;
        return Win.FsUtils.classify(path) === "file" ? String(reader.text() || "") : "";
    }

    // makes `drive` (e.g. "E:/") the key; replaces an earlier one
    function register(drive: string): bool {
        if (!drive || drive.toUpperCase().startsWith("C:"))
            return false;
        const token = Win.PasswordCheck.newSalt() + Win.PasswordCheck.newSalt();
        keyFile.path = `${drive}.aether-key`;
        keyFile.setText(token);
        if (readText(`${drive}.aether-key`).trim() !== token)
            return false;   // read-only or gone
        const salt = Win.PasswordCheck.newSalt();
        data.salt = salt;
        data.hash = Win.PasswordCheck.hashPin(token, salt);
        data.label = drive.slice(0, 2);
        Win.FsUtils.makePath(root.store.replace(/\/[^\/]*$/, ""));
        storeFile.writeAdapter();
        return true;
    }

    function unregister(): void {
        data.salt = "";
        data.hash = "";
        data.label = "";
        storeFile.writeAdapter();
        root.present = false;
    }

    function check(): void {
        if (!root.registered) {
            root.present = false;
            return;
        }
        for (const d of root.drives()) {
            if (Win.FsUtils.classify(`${d}.aether-key`) !== "file")
                continue;
            const token = readText(`${d}.aether-key`).trim();
            if (token && Win.PasswordCheck.hashPin(token, data.salt) === data.hash) {
                if (!root.present) {
                    root.present = true;
                    root.found();
                }
                return;
            }
        }
        root.present = false;
    }

    Timer {
        running: root.watching && root.registered
        interval: 1000
        repeat: true
        triggeredOnStart: true
        onTriggered: root.check()
    }

    FileView {
        id: reader

        blockLoading: true
        printErrors: false
    }

    FileView {
        id: keyFile

        blockWrites: true
        printErrors: false
    }

    FileView {
        id: storeFile

        path: root.store
        blockWrites: true
        watchChanges: true
        printErrors: false
        onFileChanged: reload()

        JsonAdapter {
            id: data

            property string salt: ""
            property string hash: ""
            property string label: ""
        }
    }
}
