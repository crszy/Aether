// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings
// HyprlandPage, KeybindsPage and MonitorsPage. MIT License, Copyright (c) 2026 shadowww-dev - see
// LICENSE-DarkShadowShell in this folder.
//
// DSS's Linux pages, made real on Windows:
//   windows  - Hyprland options -> the same keys in ~/.config/hypr/hyprland.conf, which AetherWM reads (HyprConf).
//              Only what AetherWM honours is here: no blur, shadow, opacity or dim (it does not draw those).
//   keybinds - the binds AetherWM loaded from your config, searchable; edit them in the file.
//   monitors - resolution, refresh rate, scale, rotation, primary (Windows display API, with its keep/revert check).
// SDDM (login screen) has no Windows counterpart Aether can theme; its lock screen is under Alerts > Lock screen.
import QtQuick
import Quickshell
import Quickshell.Windows as Win
import qs.services as Cae

PageScroll {
    id: page

    property string section: "windows"    // windows | keybinds | monitors
    property string bindQuery: ""

    readonly property string cfgName: Cae.HyprConf.usingLua ? "hyprland.lua" : Cae.HyprConf.usingNiri ? "config.kdl" : "hyprland.conf"
    readonly property string cfgPath: Cae.HyprConf.usingLua ? Cae.HyprConf.luaPath : Cae.HyprConf.usingNiri ? Cae.HyprConf.niriPath : Cae.HyprConf.path

    function set(key: string, value: var): void {
        Cae.HyprConf.setOption(key, String(value));
    }
    function openCfg(): void {
        Qt.openUrlExternally("file:///" + page.cfgPath);
    }

    // ---- shared header ----
    SectionTitle {
        text: page.section === "windows" ? "Windows & tiling" : page.section === "keybinds" ? "Keybinds" : "Monitors"
        sub: page.section === "monitors" ? "Your screens, through Windows. A change you don't keep is undone after a few seconds."
            : !Cae.HyprConf.loaded ? "AetherWM has no config yet: turn on Hyprland mode in Settings > Windows to make one."
            : Cae.HyprConf.usingNiri ? `AetherWM is in niri mode (${page.cfgName}): changes here last until AetherWM reloads. Edit the file to keep them.`
            : `Written to ${page.cfgName}, which AetherWM reloads on save. The file stays yours: edit it by hand too.`
    }

    // ================= windows & tiling (DSS HyprlandPage) =================
    Column {
        visible: page.section === "windows"
        width: parent.width
        spacing: 10

        SegRow {
            label: "Layout"
            options: [{ value: "dwindle", label: "Dwindle" }, { value: "master", label: "Master" }, { value: "scrolling", label: "Scrolling" }, { value: "floating", label: "Floating" }, { value: "canvas", label: "Canvas" }]
            current: Cae.HyprConf.layout
            onChosen: v => page.set("general:layout", v)
        }
        ToggleRow {
            visible: Cae.HyprConf.layout === "dwindle"
            label: "Preserve split"
            checked: Cae.HyprConf.optBool("dwindle:preserve_split", false)
            onToggled: v => page.set("dwindle:preserve_split", v)
        }
        SliderRow {
            visible: Cae.HyprConf.layout === "master"
            label: "Master size"
            from: 10
            to: 90
            step: 1
            unit: " %"
            value: Math.round(Cae.HyprConf.optReal("master:mfact", 0.55) * 100)
            onCommitted: v => page.set("master:mfact", (v / 100).toFixed(2))
        }
        SegRow {
            visible: Cae.HyprConf.layout === "master"
            label: "Master side"
            options: ["left", "right", "top", "bottom"].map(v => ({ value: v, label: v.charAt(0).toUpperCase() + v.slice(1) }))
            current: String(Cae.HyprConf.opt("master:orientation", "left"))
            onChosen: v => page.set("master:orientation", v)
        }
        ToggleRow {
            visible: Cae.HyprConf.layout === "master"
            label: "New windows become master"
            checked: String(Cae.HyprConf.opt("master:new_status", "slave")) === "master"
            onToggled: v => page.set("master:new_status", v ? "master" : "slave")
        }
        SliderRow {
            visible: Cae.HyprConf.layout === "scrolling"
            label: "Column width"
            from: 10
            to: 100
            step: 1
            unit: " %"
            value: Math.round(Cae.HyprConf.optReal("scrolling:column_width", 0.5) * 100)
            onCommitted: v => page.set("scrolling:column_width", (v / 100).toFixed(2))
        }

        SectionTitle { text: "Gaps" }
        SliderRow {
            label: "Inner gaps"
            from: 0
            to: 40
            step: 1
            unit: " px"
            value: Cae.HyprConf.optInt("general:gaps_in", 5)
            onCommitted: v => page.set("general:gaps_in", Math.round(v))
        }
        SliderRow {
            label: "Outer gaps"
            from: 0
            to: 80
            step: 1
            unit: " px"
            value: Cae.HyprConf.optInt("general:gaps_out", 20)
            onCommitted: v => page.set("general:gaps_out", Math.round(v))
        }

        SectionTitle { text: "Borders and corners" }
        SliderRow {
            label: "Border size"
            from: 0
            to: 12
            step: 1
            unit: " px"
            value: Cae.HyprConf.optInt("general:border_size", 2)
            onCommitted: v => page.set("general:border_size", Math.round(v))
        }
        SliderRow {
            label: "Corner rounding"
            from: 0
            to: 40
            step: 1
            unit: " px"
            value: Cae.HyprConf.optInt("decoration:rounding", 10)
            onCommitted: v => page.set("decoration:rounding", Math.round(v))
        }

        SectionTitle { text: "Workspaces" }
        ToggleRow {
            label: "Back and forth"
            desc: "Going to the workspace you are on takes you back to the last one"
            checked: Cae.HyprConf.optBool("binds:workspace_back_and_forth", false)
            onToggled: v => page.set("binds:workspace_back_and_forth", v)
        }

        Row {
            spacing: 8
            Btn { text: "Open " + page.cfgName; sym: "edit_document"; onClicked: page.openCfg() }
        }
    }

    // ================= keybinds (DSS KeybindsPage) =================
    Column {
        visible: page.section === "keybinds"
        width: parent.width
        spacing: 8

        Row {
            spacing: 8
            width: parent.width
            Field {
                width: parent.width - editBtn.width - 8
                placeholder: "Search binds (key, action, app)"
                onTextChanged: page.bindQuery = text.toLowerCase()
            }
            Btn { id: editBtn; text: "Edit"; sym: "edit_document"; onClicked: page.openCfg() }
        }
        T {
            visible: Cae.HyprConf.binds.length === 0
            kind: "reading"
            color: M3.inkDim
            text: "No binds loaded. AetherWM reads them from " + page.cfgName + "."
        }
        Repeater {
            model: page.section !== "keybinds" ? [] : Cae.HyprConf.binds.filter(b => {
                if (!page.bindQuery)
                    return true;
                return [b.keys, b.mods, b.key, b.dispatcher, b.args, b.description].join(" ").toLowerCase().includes(page.bindQuery);
            })
            SettingRow {
                required property var modelData
                label: modelData.description || [modelData.dispatcher, modelData.args].filter(x => !!x).join("  ")
                desc: modelData.description ? [modelData.dispatcher, modelData.args].filter(x => !!x).join("  ") : ""
                reserved: keyText.implicitWidth + 40
                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: keyText.implicitWidth + 20
                    height: 28
                    radius: 8
                    color: M3.surfaceHighest
                    T {
                        id: keyText
                        anchors.centerIn: parent
                        kind: "mono"
                        font.pixelSize: 12
                        text: modelData.keys || [modelData.mods, modelData.key].filter(x => !!x).join(" + ")
                    }
                }
            }
        }
    }

    // ================= monitors (DSS MonitorsPage) =================
    Column {
        visible: page.section === "monitors"
        width: parent.width
        spacing: 10

        Component.onCompleted: Win.Displays.refresh()

        Rectangle {
            visible: Win.Displays.pendingConfirmation
            width: parent.width
            height: 64
            radius: M3.rM
            color: M3.primaryContainer
            T {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                color: M3.inkPrimaryC
                text: `Keep these display settings? Undoing in ${Win.Displays.confirmSecondsLeft} s`
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Btn { text: "Keep"; primary: true; onClicked: Win.Displays.keepChanges() }
                Btn { text: "Undo"; onClicked: Win.Displays.revertChanges() }
            }
        }
        T {
            visible: !!Win.Displays.lastError
            color: M3.error
            text: Win.Displays.lastError
            width: parent.width
            wrapMode: Text.Wrap
        }

        Repeater {
            model: page.section === "monitors" ? Win.Displays.displays.filter(d => d.active) : []
            Column {
                id: mon
                required property var modelData
                readonly property var modes: modelData.modes ?? []
                readonly property var curMode: modes.find(m => m.width === modelData.resolutionWidth && m.height === modelData.resolutionHeight) ?? null
                width: parent.width
                spacing: 8

                SectionTitle {
                    text: `${mon.modelData.name || "Display"}${mon.modelData.primary ? "  (main)" : ""}`
                    sub: `${mon.modelData.resolutionWidth} × ${mon.modelData.resolutionHeight} at ${mon.modelData.refreshRate} Hz`
                }
                SegRow {
                    label: "Resolution"
                    // the screen's own size first, then the next few smaller ones (the full list lives in Windows' settings)
                    options: mon.modes.slice().sort((a, b) => b.width * b.height - a.width * a.height).slice(0, 4).map(m => ({ value: `${m.width}x${m.height}`, label: `${m.width}×${m.height}` }))
                    current: `${mon.modelData.resolutionWidth}x${mon.modelData.resolutionHeight}`
                    onChosen: v => {
                        const wh = v.split("x").map(Number);
                        const m = mon.modes.find(x => x.width === wh[0] && x.height === wh[1]);
                        const rates = (m?.rates ?? []).slice().sort((a, b) => b - a);
                        const rate = rates.includes(mon.modelData.refreshRate) ? mon.modelData.refreshRate : (rates[0] ?? mon.modelData.refreshRate);
                        Win.Displays.setMode(mon.modelData.id, wh[0], wh[1], rate);
                    }
                }
                SegRow {
                    visible: (mon.curMode?.rates ?? []).length > 1
                    label: "Refresh rate"
                    options: (mon.curMode?.rates ?? []).slice().sort((a, b) => b - a).slice(0, 5).map(r => ({ value: String(r), label: `${r} Hz` }))
                    current: String(mon.modelData.refreshRate)
                    onChosen: v => Win.Displays.setMode(mon.modelData.id, mon.modelData.resolutionWidth, mon.modelData.resolutionHeight, Number(v))
                }
                SegRow {
                    visible: !!mon.modelData.scaleSupported && (mon.modelData.scales ?? []).length > 1
                    label: "Scale"
                    options: (mon.modelData.scales ?? []).slice(0, 6).map(s => ({ value: String(s), label: `${s} %` }))
                    current: String(mon.modelData.scale)
                    onChosen: v => Win.Displays.setScale(mon.modelData.id, Number(v))
                }
                SegRow {
                    label: "Rotation"
                    options: [{ value: "0", label: "0°" }, { value: "90", label: "90°" }, { value: "180", label: "180°" }, { value: "270", label: "270°" }]
                    current: String(mon.modelData.rotation ?? 0)
                    onChosen: v => Win.Displays.setRotation(mon.modelData.id, Number(v))
                }
                ToggleRow {
                    visible: !mon.modelData.primary
                    label: "Make this the main display"
                    checked: false
                    onToggled: v => {
                        if (v)
                            Win.Displays.setPrimary(mon.modelData.id);
                    }
                }
            }
        }

        SectionTitle { text: "Login screen"; sub: "DarkShadowShell themes SDDM on Linux. Windows' sign-in screen is not Aether's to theme; Aether's own lock screen is." }
        Row {
            spacing: 8
            Btn { text: "Lock screen settings"; sym: "lock"; onClicked: AetherSettings.open("lock") }
        }
    }
}
