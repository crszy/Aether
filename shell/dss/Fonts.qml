// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick

// The fonts you picked in Settings > Appearance > Fonts (~/.config/rice/fonts.json, written by font-set).
// Since V3.23 the interface uses Google Sans Flex (rice-fonts installs it), the terminal keeps JetBrainsMono Nerd Font.
// A family that is not installed falls back to Google Sans Flex, Rubik or Inter, so text never turns into boxes.
Singleton {
    id: f
    readonly property string nerdDef: "JetBrainsMono Nerd Font"
    readonly property var fams: Qt.fontFamilies()
    function has(n) { return fams.indexOf(n) >= 0 }
    readonly property bool symbols: has("Material Symbols Rounded")
    readonly property string ui: has("Google Sans Flex") ? "Google Sans Flex" : (has("Rubik") ? "Rubik" : (has("Inter") ? "Inter" : nerdDef))
    readonly property string code: has("Google Sans Code") ? "Google Sans Code" : nerdDef

    // what fonts.json says ("" = not chosen); version 1 files used JetBrains for everything, that counts as not chosen
    property int v: 2
    property string rMain: ""
    property string rNumbers: ""
    property string rTitle: ""
    property string rReading: ""
    property string rMono: ""
    property string rIcons: ""
    function pick(r) { return r === "" || (v < 2 && r === nerdDef) || !has(r) ? ui : r }

    readonly property string main: pick(rMain)
    readonly property string numbers: pick(rNumbers)
    readonly property string title: pick(rTitle)
    readonly property string reading: pick(rReading)
    readonly property string mono: rMono !== "" && has(rMono) ? rMono : nerdDef
    readonly property string icons: rIcons !== "" && has(rIcons) ? rIcons : nerdDef

    function family(kind) {
        switch (kind) {
        case "title": return title
        case "number": return numbers
        case "mono": return mono
        case "code": return code
        case "icon": return icons
        case "reading": return reading
        default: return main
        }
    }

    DssProc {
        id: proc
        command: ["bash", "-c", "cat \"$HOME/.config/rice/fonts.json\" 2>/dev/null"]
        running: true
        stdout: DssCollector {
            onStreamFinished: {
                let j = {}
                try { j = JSON.parse(text) } catch (e) { j = {} }
                f.v = j.v || (text.trim() === "" ? 2 : 1)
                f.rMain = j.main || ""; f.rNumbers = j.numbers || ""; f.rTitle = j.title || ""
                f.rReading = j.reading || ""; f.rMono = j.mono || ""; f.rIcons = j.icons || ""
            }
        }
    }
    Timer {
        interval: 1500
        running: true
        repeat: true
        onTriggered: { if (!proc.running) proc.running = true }
    }
}
