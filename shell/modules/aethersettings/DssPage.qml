// DarkShadowShell's settings (https://github.com/shadowww-dev/DarkShadowShell, MIT, Copyright (c) 2026 shadowww-dev)
// as pages of the merged Aether Settings panel: its bar and island (~/.config/rice/bar.json), desktop widgets and dock
// (~/.config/rice/desktop.json). Reads and writes go through dss/DssWin (the same code DSS's own "rice-bar" and
// "desktop-set" calls use), and the DSS parts (Mix & match = "dss") pick the files up live.
// DSS's Linux pages (Hyprland, keybinds, monitors) are ported as Windows equivalents in WmPage.qml. SDDM and GRUB have none.
import QtQuick
import qs.dss as Dss
import qs.services as Cae

PageScroll {
    id: page

    property string section: "bar"     // bar | widgets | dock
    property var bar: ({})
    property var desk: ({})

    function load(): void {
        try {
            page.bar = JSON.parse(Dss.DssWin.riceBar(["state"]).out || "{}");
        } catch (e) {
            page.bar = {};
        }
        try {
            page.desk = JSON.parse(Dss.DssWin.desktopSet(["state"]).out || "{}");
        } catch (e) {
            page.desk = {};
        }
    }
    function setBar(k: string, v: var): void {
        Dss.DssWin.riceBar(["set", k, String(v)]);
        page.load();
    }
    function setDesk(args: var): void {
        Dss.DssWin.desktopSet(args.map(String));
        page.load();
    }
    function opts(list: var): var {
        return list.map(v => ({ value: v, label: v.charAt(0).toUpperCase() + v.slice(1) }));
    }
    function b(k: string): bool {
        return page.bar[k] === true;
    }
    function w(id: string): bool {
        return !!(page.desk.widgets && page.desk.widgets[id] && page.desk.widgets[id].enabled);
    }
    function dk(k: string): var {
        return page.desk.dock ? page.desk.dock[k] : undefined;
    }

    Component.onCompleted: load()

    // ---- shared header ----
    SectionTitle {
        text: page.section === "bar" ? "DarkShadowShell bar" : page.section === "widgets" ? "DarkShadowShell desktop widgets" : "DarkShadowShell dock"
        sub: (page.section === "bar" ? Cae.WinSettings.mixBar : page.section === "widgets" ? Cae.WinSettings.mixDesktopWidgets : Cae.WinSettings.mixDock) === "dss"
            ? "In use now (Mix & match). Changes show straight away."
            : "Not in use: pick DarkShadowShell for this part in Mix & match to see it. Settings are kept either way."
    }

    // ================= bar =================
    Column {
        visible: page.section === "bar"
        width: parent.width
        spacing: 10

        SegRow {
            label: "Style"
            options: page.opts(Dss.DssWin.barChoices.style)
            current: page.bar.style || "dynamic"
            onChosen: v => page.setBar("style", v)
        }
        SegRow {
            label: "Position"
            options: page.opts(Dss.DssWin.barChoices.position)
            current: page.bar.position || "top"
            onChosen: v => page.setBar("position", v)
        }
        SliderRow {
            label: "Height"
            from: 28
            to: 56
            step: 1
            unit: " px"
            value: page.bar.height || 38
            onCommitted: v => page.setBar("height", Math.round(v))
        }
        SliderRow {
            label: "Opacity"
            from: 0.3
            to: 1
            step: 0.01
            decimals: 2
            value: page.bar.opacity || 0.72
            onCommitted: v => page.setBar("opacity", v.toFixed(2))
        }
        ToggleRow { label: "Auto-hide"; checked: page.b("autohide"); onToggled: v => page.setBar("autohide", v) }
        ToggleRow { label: "Shadow"; checked: page.b("shadow"); onToggled: v => page.setBar("shadow", v) }
        ToggleRow { label: "Edge frame"; desc: "The thin frame round the screen in the Edge style"; checked: page.b("edge_frame"); onToggled: v => page.setBar("edge_frame", v) }
        ToggleRow { label: "Seconds on the clock"; checked: page.b("seconds"); onToggled: v => page.setBar("seconds", v) }

        SectionTitle { text: "Workspaces" }
        SegRow {
            label: "Look"
            options: page.opts(Dss.DssWin.barChoices.ws_style)
            current: page.bar.ws_style || "dots"
            onChosen: v => page.setBar("ws_style", v)
        }
        SliderRow {
            label: "Shown"
            from: 3
            to: 10
            step: 1
            value: page.bar.workspaces || 5
            onCommitted: v => page.setBar("workspaces", Math.round(v))
        }

        SectionTitle { text: "Logo and title" }
        SegRow {
            label: "Logo colour"
            options: page.opts(Dss.DssWin.barChoices.logo_color)
            current: page.bar.logo_color || "default"
            onChosen: v => page.setBar("logo_color", v)
        }
        SegRow {
            label: "Window icon"
            options: page.opts(Dss.DssWin.barChoices.title_icon)
            current: page.bar.title_icon || "right"
            onChosen: v => page.setBar("title_icon", v)
        }
        SliderRow {
            label: "Tray icon size"
            from: 12
            to: 28
            step: 1
            unit: " px"
            value: page.bar.tray_size || 18
            onCommitted: v => page.setBar("tray_size", Math.round(v))
        }

        SectionTitle { text: "Dynamic island"; sub: "The pill in the middle that grows to show what is going on" }
        ToggleRow { label: "Dynamic"; desc: "Grow for media, volume and alerts"; checked: page.b("island_dynamic"); onToggled: v => page.setBar("island_dynamic", v) }
        ToggleRow { label: "Open on hover"; checked: page.b("island_hover"); onToggled: v => page.setBar("island_hover", v) }
        ToggleRow { label: "Volume"; checked: page.b("island_volume"); onToggled: v => page.setBar("island_volume", v) }
        ToggleRow { label: "Workspace changes"; checked: page.b("island_ws"); onToggled: v => page.setBar("island_ws", v) }
        ToggleRow { label: "Notifications"; checked: page.b("island_notifs"); onToggled: v => page.setBar("island_notifs", v) }
        ToggleRow { label: "Charging"; checked: page.b("island_charge"); onToggled: v => page.setBar("island_charge", v) }
        ToggleRow { label: "Date"; checked: page.b("island_date"); onToggled: v => page.setBar("island_date", v) }
        ToggleRow { label: "Weather"; checked: page.b("island_weather"); onToggled: v => page.setBar("island_weather", v) }
        SegRow {
            label: "Opens on"
            options: page.opts(Dss.DssWin.barChoices.island_tab)
            current: page.bar.island_tab || "today"
            onChosen: v => page.setBar("island_tab", v)
        }
        SliderRow {
            label: "Closes after"
            from: 2
            to: 10
            step: 1
            unit: " s"
            value: page.bar.island_close || 4
            onCommitted: v => page.setBar("island_close", Math.round(v))
        }

        SectionTitle { text: "Layout"; sub: "Drag modules in DSS's own bar editor (right-click the bar), or put the default back here" }
        Btn {
            text: "Reset bar layout"
            onClicked: {
                Dss.DssWin.riceBar(["layout-reset"]);
                page.load();
            }
        }
    }

    // ================= desktop widgets =================
    Column {
        visible: page.section === "widgets"
        width: parent.width
        spacing: 10

        SectionTitle { text: "Look" }
        SegRow {
            label: "Theme"
            options: page.opts(Dss.DssWin.dThemes)
            current: page.desk.widgetTheme || "google"
            onChosen: v => page.setDesk(["widgets-theme", v])
        }
        ToggleRow { label: "Blur behind widgets"; checked: page.desk.widgetBlur === true; onToggled: v => page.setDesk(["widgets-blur", v]) }
        ToggleRow { label: "Match the wallpaper"; desc: "Light or dark text by how bright the wallpaper is"; checked: page.desk.widgetAdapt === true; onToggled: v => page.setDesk(["widgets-adapt", v]) }
        ToggleRow { label: "Hide when windows cover the desktop"; checked: page.desk.autoHide === true; onToggled: v => page.setDesk(["auto-hide", v]) }
        SliderRow {
            label: "Opacity"
            from: 0.2
            to: 1
            step: 0.01
            decimals: 2
            value: page.desk.widgetOpacity !== undefined ? page.desk.widgetOpacity : 1
            onCommitted: v => page.setDesk(["widgets-opacity", v.toFixed(2)])
        }
        Btn {
            text: page.desk.editing ? "Done moving widgets" : "Move widgets on the desktop"
            onClicked: page.setDesk(["edit", page.desk.editing ? "off" : "on"])
        }

        SectionTitle { text: "Clock" }
        ToggleRow { label: "Desktop clock"; checked: !!(page.desk.clock && page.desk.clock.enabled); onToggled: v => page.setDesk(["clock", "enabled", v]) }
        SegRow {
            label: "Style"
            options: page.opts(Dss.DssWin.dChoices.style)
            current: page.desk.clock ? page.desk.clock.style : "digital"
            onChosen: v => page.setDesk(["clock", "style", v])
        }
        SegRow {
            label: "Font"
            options: page.opts(Dss.DssWin.dChoices.font)
            current: page.desk.clock ? page.desk.clock.font : "bebas"
            onChosen: v => page.setDesk(["clock", "font", v])
        }
        SegRow {
            label: "Placement"
            options: [{ value: "draggable", label: "Where I put it" }, { value: "leastBusy", label: "Quietest spot" }, { value: "mostBusy", label: "Busiest spot" }]
            current: page.desk.clock ? page.desk.clock.placement : "draggable"
            onChosen: v => page.setDesk(["clock", "placement", v])
        }

        SectionTitle { text: "Widgets"; sub: "Each one appears on the desktop; move them with the button above" }
        Repeater {
            model: Dss.DssWin.dWidgets
            ToggleRow {
                required property string modelData
                label: modelData === "usercard" ? "User card" : modelData === "nowring" ? "Now playing ring" : modelData === "playpill" ? "Player pill"
                    : modelData === "datepill" ? "Date pill" : modelData.charAt(0).toUpperCase() + modelData.slice(1)
                checked: page.w(modelData)
                onToggled: v => page.setDesk(["widget", modelData, v])
            }
        }
    }

    // ================= dock =================
    Column {
        visible: page.section === "dock"
        width: parent.width
        spacing: 10

        SegRow {
            label: "Position"
            options: page.opts(["bottom", "left", "right"])
            current: page.dk("position") || "bottom"
            onChosen: v => page.setDesk(["dock", "position", v])
        }
        SliderRow {
            label: "Opacity"
            from: 0.2
            to: 1
            step: 0.01
            decimals: 2
            value: page.dk("opacity") !== undefined ? page.dk("opacity") : 0.78
            onCommitted: v => page.setDesk(["dock", "opacity", v.toFixed(2)])
        }
        ToggleRow { label: "Background"; checked: page.dk("background") === true; onToggled: v => page.setDesk(["dock", "background", v]) }
        ToggleRow { label: "Show on hover only"; checked: page.dk("hover") === true; onToggled: v => page.setDesk(["dock", "hover", v]) }
        ToggleRow { label: "Keep space for it"; desc: "Windows don't go under the dock"; checked: page.dk("reserve") === true; onToggled: v => page.setDesk(["dock", "reserve", v]) }
        ToggleRow { label: "Tint with the accent"; checked: page.dk("tint") === true; onToggled: v => page.setDesk(["dock", "tint", v]) }
        ToggleRow { label: "Shade"; checked: page.dk("shade") === true; onToggled: v => page.setDesk(["dock", "shade", v]) }
        ToggleRow { label: "Media controls"; checked: page.dk("media") === true; onToggled: v => page.setDesk(["dock", "media", v]) }
        ToggleRow { label: "Now playing text"; checked: page.dk("nowText") === true; onToggled: v => page.setDesk(["dock", "nowText", v]) }
        ToggleRow { label: "Pin button"; checked: page.dk("pinButton") === true; onToggled: v => page.setDesk(["dock", "pinButton", v]) }
        ToggleRow { label: "Apps button"; checked: page.dk("appsButton") === true; onToggled: v => page.setDesk(["dock", "appsButton", v]) }
        ToggleRow { label: "App menu on right-click"; checked: page.dk("appMenu") === true; onToggled: v => page.setDesk(["dock", "appMenu", v]) }

        SectionTitle { text: "Pinned apps"; sub: "Right-click an app in the dock to pin or unpin it" }
        Repeater {
            model: page.dk("apps") || []
            SettingRow {
                required property string modelData
                label: modelData
                reserved: 120
                Btn {
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Unpin"
                    onClicked: page.setDesk(["dock-unpin", modelData])
                }
            }
        }
    }
}
