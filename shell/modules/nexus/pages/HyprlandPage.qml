pragma ComponentBehavior: Bound

// Settings -> Hyprland: Hyprland mode for Windows. Everything here is written straight into
// ~/.config/hypr/hyprland.conf (your file stays the source of truth - edit it by hand too, it reloads on save).
import QtQuick
import QtQuick.Layouts
import Quickshell
import Caelestia.Config
import Caelestia.I18n
import qs.components
import qs.services
import qs.modules.nexus.common

PageBase {
    id: root

    title: Tr.tr("Hyprland")

    property bool listsReady: false
    readonly property Timer listsTimer: Timer {
        interval: 60
        running: true
        onTriggered: root.listsReady = true
    }

    readonly property var layouts: [
        { id: "dwindle", name: Tr.tr("Dwindle"), sub: Tr.tr("Each new window splits the focused one") },
        { id: "master", name: Tr.tr("Master"), sub: Tr.tr("One big window, the rest stacked beside it") },
        { id: "scrolling", name: Tr.tr("Scrolling"), sub: Tr.tr("niri-style columns that scroll sideways") },
        { id: "canvas", name: Tr.tr("Infinite canvas"), sub: Tr.tr("InfiniteDesk: pan and zoom one endless desktop") },
        { id: "floating", name: Tr.tr("Floating"), sub: Tr.tr("No tiling - binds, borders and corners only") }
    ]

    function set(key: string, value: var): void {
        HyprConf.setOption(key, String(value));
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: root.cappedWidth
        spacing: Tokens.spacing.extraSmall / 2

        SectionHeader {
            first: true
            text: Tr.tr("Hyprland mode")
        }

        ToggleRow {
            first: true
            text: Tr.tr("Run Windows like Hyprland")
            subtext: Tr.tr("Tiles your open windows and turns on the binds in hyprland.conf. Off = Windows as normal")
            checked: WinSettings.hyprland
            onToggled: WinSettings.hyprland = checked
        }

        ToggleRow {
            text: Tr.tr("Hyprland window manager (beta)")
            subtext: WinSettings.komorebi
                ? Tr.tr("AetherWM runs hyprland.conf itself: tiling, binds and drags, no whkd. Replaces the mode above")
                : Tr.tr("Needs AetherWM - build it first in Aether settings > Extras (no Visual Studio needed)")
            enabled: !!WinSettings.komorebi
            checked: WinSettings.hyprlandWm
            onToggled: WinSettings.hyprlandWm = checked
        }

        ToggleRow {
            text: Tr.tr("Alt works like Super")
            subtext: Tr.tr("Every SUPER bind also works with ALT, and Super keeps working. Windows keeps Super+L for locking the PC, so a SUPER, L bind only works as Alt+L")
            visible: WinSettings.hyprlandWm
            checked: WinSettings.hyprlandAlt
            onToggled: WinSettings.hyprlandAlt = checked
        }

        InfoRow {
            label: HyprConf.loaded ? (HyprConf.usingLua ? "hyprland.lua" : HyprConf.usingNiri ? "niri config.kdl" : "hyprland.conf") : Tr.tr("No hyprland.conf yet")
            subtext: HyprConf.loaded
                ? Tr.tr("%1 binds, %2 notes, %3 file(s) - %4").arg(HyprConf.binds.length).arg(HyprConf.notes.length).arg(HyprConf.files.length).arg((HyprConf.usingLua ? HyprConf.luaPath : HyprConf.usingNiri ? HyprConf.niriPath : HyprConf.path).replace(/\//g, "\\"))
                : (HyprConf.error || Tr.tr("One is made when you turn Hyprland mode on"))
        }

        RowButton {
            icon: "edit_document"
            text: HyprConf.usingLua ? Tr.tr("Open hyprland.lua") : HyprConf.usingNiri ? Tr.tr("Open config.kdl") : Tr.tr("Open hyprland.conf")
            subtext: Tr.tr("Saving it applies the changes straight away")
            onClicked: HyprConf.openConf()
        }

        RowButton {
            last: true
            icon: "refresh"
            text: Tr.tr("Reload")
            subtext: Tr.tr("Same as hyprctl reload")
            onClicked: HyprConf.reload()
        }

        // ---------------- layout ----------------
        SectionHeader {
            text: Tr.tr("Layout")
        }

        Repeater {
            model: root.layouts

            RowButton {
                required property var modelData
                required property int index

                first: index === 0
                last: index === root.layouts.length - 1
                icon: ["account_tree", "view_sidebar", "view_week", "open_with", "select_window"][index]
                text: modelData.name
                subtext: modelData.sub
                trailingIcon: HyprConf.layout === modelData.id ? "check_circle" : "radio_button_unchecked"
                onClicked: root.set("general:layout", modelData.id)
            }
        }

        // ---------------- look ----------------
        SectionHeader {
            text: Tr.tr("Gaps, borders and corners")
        }

        StepperRow {
            first: true
            label: Tr.tr("Gaps between windows")
            subtext: "general:gaps_in"
            value: HyprConf.optInt("general:gaps_in", 5)
            from: 0
            to: 60
            onMoved: v => root.set("general:gaps_in", v)
        }

        StepperRow {
            label: Tr.tr("Gaps at the screen edge")
            subtext: "general:gaps_out"
            value: HyprConf.optInt("general:gaps_out", 20)
            from: 0
            to: 120
            onMoved: v => root.set("general:gaps_out", v)
        }

        StepperRow {
            label: Tr.tr("Border size")
            subtext: Tr.tr("0 = no border. Windows 11 draws a 1 px coloured border")
            value: HyprConf.optInt("general:border_size", 2)
            from: 0
            to: 10
            onMoved: v => root.set("general:border_size", v)
        }

        TextFieldRow {
            label: Tr.tr("Focused border colour")
            subtext: "general:col.active_border - rgba(33ccffee), rgb(..), 0xAARRGGBB"
            value: String(HyprConf.opt("general:col.active_border", ""))
            onEditingFinished: v => root.set("general:col.active_border", v)
        }

        TextFieldRow {
            label: Tr.tr("Other windows' border colour")
            subtext: "general:col.inactive_border"
            value: String(HyprConf.opt("general:col.inactive_border", ""))
            onEditingFinished: v => root.set("general:col.inactive_border", v)
        }

        StepperRow {
            last: true
            label: Tr.tr("Rounding")
            subtext: Tr.tr("0 = square, 1-6 = small round, more = round (what Windows can draw)")
            value: HyprConf.optInt("decoration:rounding", 10)
            from: 0
            to: 30
            onMoved: v => root.set("decoration:rounding", v)
        }

        // ---------------- layouts' options ----------------
        SectionHeader {
            text: Tr.tr("Layout options")
        }

        ToggleRow {
            first: true
            text: Tr.tr("Dwindle: keep the split direction")
            subtext: "dwindle:preserve_split"
            checked: HyprConf.optBool("dwindle:preserve_split", false)
            onToggled: root.set("dwindle:preserve_split", checked)
        }

        StepperRow {
            label: Tr.tr("Master: size of the master window (%)")
            subtext: "master:mfact"
            value: Math.round(HyprConf.optReal("master:mfact", 0.55) * 100)
            from: 10
            to: 90
            stepSize: 5
            onMoved: v => root.set("master:mfact", (v / 100).toFixed(2))
        }

        ToggleRow {
            text: Tr.tr("Master: new windows become the master")
            subtext: "master:new_status"
            checked: String(HyprConf.opt("master:new_status", "slave")) === "master"
            onToggled: root.set("master:new_status", checked ? "master" : "slave")
        }

        Repeater {
            model: ["left", "right", "top", "bottom", "center"]

            RowButton {
                required property string modelData

                icon: "align_horizontal_left"
                text: Tr.tr("Master on the %1").arg(modelData)
                trailingIcon: String(HyprConf.opt("master:orientation", "left")) === modelData ? "check_circle" : "radio_button_unchecked"
                onClicked: root.set("master:orientation", modelData)
            }
        }

        StepperRow {
            label: Tr.tr("Scrolling: column width (% of the screen)")
            subtext: "scrolling:column_width"
            value: Math.round(HyprConf.optReal("scrolling:column_width", 0.5) * 100)
            from: 10
            to: 100
            stepSize: 5
            onMoved: v => root.set("scrolling:column_width", (v / 100).toFixed(2))
        }

        ToggleRow {
            last: true
            text: Tr.tr("Pressing the current workspace goes back to the last one")
            subtext: "binds:workspace_back_and_forth"
            checked: HyprConf.optBool("binds:workspace_back_and_forth", false)
            onToggled: root.set("binds:workspace_back_and_forth", checked)
        }

        // ---------------- binds ----------------
        SectionHeader {
            text: Tr.tr("Binds from hyprland.conf (%1)").arg(HyprConf.binds.length)
        }

        InfoRow {
            visible: HyprConf.binds.length === 0
            first: true
            last: true
            label: Tr.tr("No binds")
            subtext: Tr.tr("Add lines like: bind = SUPER, Return, exec, wt.exe")
        }

        Repeater {
            model: root.listsReady ? HyprConf.binds : []

            InfoRow {
                required property var modelData
                required property int index

                first: index === 0
                last: index === HyprConf.binds.length - 1
                label: modelData.keys
                subtext: `${modelData.description || (modelData.dispatcher + " " + modelData.args)}   ·   line ${modelData.line}`
            }
        }

        // ---------------- notes ----------------
        SectionHeader {
            text: Tr.tr("Notes (%1)").arg(HyprConf.notes.length)
        }

        InfoRow {
            visible: HyprConf.notes.length === 0
            first: true
            last: true
            label: Tr.tr("Nothing to report")
            subtext: Tr.tr("Every line in your config is used")
        }

        Repeater {
            model: root.listsReady ? HyprConf.notes.slice(0, 120) : []

            InfoRow {
                required property var modelData
                required property int index

                first: index === 0
                last: index === Math.min(120, HyprConf.notes.length) - 1
                label: `${modelData.file.split("/").pop()}:${modelData.line}   ${modelData.text}`
                subtext: modelData.why
            }
        }
    }
}
