//@ pragma DefaultEnv QS_NO_RELOAD_POPUP=1
//@ pragma DefaultEnv QSG_RENDER_LOOP=threaded

// Aether's setup, built from Caelestia's own Settings parts (nav rows, shell rows, select rows, toggles, its
// buttons and its colours) so it looks like the shell it installs. AetherSetup.exe unpacks Aether to a temp
// folder and runs this file from that copy's config folder (so qs.* is Caelestia's config). AETHER_SRC = the
// unpacked files. Nothing is written until "Install": the choices go into windows.json then.
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import Quickshell
import Quickshell.Io
import Quickshell.Windows as Win
import Caelestia.Config
import Caelestia.Multimedia
import qs.components
import qs.components.controls
import qs.components.containers
import qs.services
import qs.modules.nexus.common

ShellRoot {
    id: shellRoot

    settings.watchFiles: false

    readonly property string src: (Quickshell.env("AETHER_SRC") || Quickshell.shellDir + "/..").replace(/\\/g, "/")
    readonly property string media: `${src}/setup-ui/media`
    readonly property string profile: (Quickshell.env("USERPROFILE") || "").replace(/\\/g, "/")
    readonly property var ui: win
    readonly property string localApp: (Quickshell.env("LOCALAPPDATA") || "").replace(/\\/g, "/")

    FontLoader {
        source: Quickshell.shellPath("assets/google-sans-flex/GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf")
    }

    FloatingWindow {
        id: win

        // ---------------- choices ----------------
        property int step: Number(Quickshell.env("AETHER_SETUP_STEP") || 0) // (env: testing)
        readonly property var steps: [
            { icon: "home", label: "Welcome", description: "What Aether is" },
            { icon: "palette", label: "Your look", description: "Which shell your desktop looks like" },
            { icon: "dashboard_customize", label: "Rice builder", description: "Who draws each part" },
            { icon: "tune", label: "Install options", description: "Folder, startup, extras" },
            { icon: "download", label: "Install", description: "Copy Aether to this PC" }
        ]
        property string target: shellRoot.localApp + "/Programs/Aether Shell"
        readonly property string configDir: shellRoot.profile + "/.config/caelestia"
        readonly property bool hasSettings: Win.FsUtils.classify(configDir + "/windows.json") === "file"
        property bool keepSettings: hasSettings
        property string look: Quickshell.env("AETHER_SETUP_LOOK") || "caelestia" // (env: testing)
        property var mix: mixFor(look)
        property string titleBars: "themed"
        property bool hideTaskbar: true
        property bool startMenu: true
        property bool desktopLink: false
        property bool autostart: true
        property bool batterySaver: true
        property bool tilingAgreed: false
        property bool bigPreview: false

        // install progress
        property string phase: "" // "" | stopping | copying | finishing | done | failed
        property int totalFiles: 1
        property int copied: 0
        property string lastFile: ""
        property string error: ""

        readonly property var shells: [
            { id: "caelestia", name: "Caelestia", by: "caelestia-dots", sub: "Side bar, drawers that grow out of it, dashboard" },
            { id: "end4", name: "end-4 · illogical-impulse", by: "end-4", sub: "Top bar, sidebars, overview" },
            { id: "dms", name: "DankMaterialShell", by: "AvengeMedia / duxlabs", sub: "Top bar, dock, control centre, desktop widgets" },
            { id: "noctalia", name: "Noctalia", by: "noctalia-dev", sub: "Top bar, dock, lots of widgets" },
            { id: "zenith", name: "Zenith", by: "zaeemali272", sub: "Dynamic island bar, focus tools" }
        ]
        function shellName(id: string): string {
            return ({ caelestia: "Caelestia", end4: "end-4", dms: "DankMaterialShell", noctalia: "Noctalia", zenith: "Zenith", off: "Off" })[id] ?? id;
        }
        function mixFor(id: string): var {
            const all = k => ({ mixBar: k, mixLauncher: k, mixNotifs: k, mixOsd: k });
            switch (id) {
            case "dms":
                return Object.assign(all("dms"), { mixLock: "caelestia", mixWallpaper: "dms", mixDock: "dms", mixDesktopWidgets: "dms" });
            case "noctalia":
                return Object.assign(all("noctalia"), { mixLock: "caelestia", mixWallpaper: "noctalia", mixDock: "noctalia", mixDesktopWidgets: "noctalia" });
            case "end4":
                return Object.assign(all("end4"), { mixLock: "caelestia", mixWallpaper: "caelestia", mixDock: "off", mixDesktopWidgets: "off" });
            case "zenith":
                return Object.assign(all("zenith"), { mixLock: "caelestia", mixWallpaper: "caelestia", mixDock: "off", mixDesktopWidgets: "off" });
            default:
                return Object.assign(all("caelestia"), { mixLock: "caelestia", mixWallpaper: "caelestia", mixDock: "off", mixDesktopWidgets: "off" });
            }
        }
        onLookChanged: mix = mixFor(look)
        function setPiece(key: string, value: string): void {
            const m = Object.assign({}, mix);
            m[key] = value;
            mix = m;
        }
        function shots(id: string): var {
            const dir = `${shellRoot.media}/${id}`;
            return Win.FsUtils.listDir(dir).filter(n => /^\d+\.(jpe?g|png)$/i.test(n)).sort((a, b) => parseInt(a) - parseInt(b)).map(n => `file:///${dir}/${n}`);
        }
        function video(id: string): string {
            const f = `${shellRoot.media}/${id}/showcase.mp4`;
            return Win.FsUtils.classify(f) === "file" ? f : "";
        }

        // ---------------- install ----------------
        // The window only collects the choices: "Install" writes them to install-plan.json next to the unpacked
        // files and closes; AetherSetup.exe then MOVES the unpacked folder into place (instant on the same drive -
        // no second copy for Windows Defender to scan), writes the settings, makes the shortcuts and starts Aether.
        function startInstall(): void {
            phase = "finishing";
            error = "";
            planFile.setText(JSON.stringify({
                target: target.split("/").join(String.fromCharCode(92)),
                keepSettings: keepSettings,
                settingsJson: keepSettings ? "" : settingsJson(),
                configDir: configDir.split("/").join(String.fromCharCode(92)),
                startMenu: startMenu,
                desktop: desktopLink,
                autostart: autostart,
                tiling: tilingAgreed
            }, null, 2));
            quitTimer.start();
        }
        function settingsJson(): string {
            return JSON.stringify(Object.assign({}, mix, {
                titleBars: titleBars,
                hideTaskbar: hideTaskbar,
                batterySaver: batterySaver ? "auto" : "off",
                settingsStyle: "caelestia",
                nexusFx: "video",
                riceBackground: `${target}/media/aether-background.mp4`,
                setupPending: false
            }), null, 4) + String.fromCharCode(10);
        }

        FileView {
            id: planFile
            path: shellRoot.src + "/install-plan.json"
            blockWrites: true
            printErrors: false
        }
        // testing only: AETHER_SETUP_AUTOINSTALL=1 presses Install once the window is up
        Timer {
            running: Quickshell.env("AETHER_SETUP_AUTOINSTALL") === "1"
            interval: 4000
            onTriggered: win.startInstall()
        }
        Timer {
            id: quitTimer
            interval: 300
            onTriggered: Qt.quit()
        }
        FolderDialog {
            id: folderDialog
            title: "Install Aether to"
            onAccepted: win.target = decodeURIComponent(selectedFolder.toString().replace(/^file:\/{2,3}/, "")) + "/Aether Shell"
        }

        // ---------------- window ----------------
        title: "Aether Setup"
        color: Qt.alpha(Colours.palette.m3surface, 0.9)
        surfaceFormat.opaque: false
        implicitWidth: Math.min(1240, (screen?.width ?? 1280) - 60)
        implicitHeight: Math.min(760, (screen?.height ?? 800) - 60)

        Component.onCompleted: frameTimer.start()
        Timer {
            id: frameTimer
            interval: 80
            onTriggered: {
                const w = root.Window.window;
                if (!w)
                    return;
                Win.WindowFrame.hideTitleBar(w);
                Win.WindowFrame.setBackdrop(w, "acrylic", !Colours.light);
                Win.WindowFrame.bringToFront(w);
            }
        }

        Item {
            id: root

            anchors.fill: parent

            // the video behind it, like Caelestia's Settings background
            Visual {
                anchors.fill: parent
                source: Win.FsUtils.classify(`${shellRoot.src}/media/aether-background.mp4`) === "file" ? `${shellRoot.src}/media/aether-background.mp4` : ""
                playing: true
                opacity: 0.35
            }

            DragHandler {
                target: null
                onActiveChanged: if (active) root.Window.window.startSystemMove()
            }

            // ---- left: Aether and the steps, drawn like Caelestia's Settings nav
            ColumnLayout {
                id: nav

                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.margins: Tokens.padding.large
                width: Math.min(380, parent.width * 0.32)
                spacing: Tokens.spacing.extraSmall

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Tokens.padding.medium
                    Layout.bottomMargin: Tokens.spacing.large
                    spacing: Tokens.spacing.medium

                    Logo {
                        implicitWidth: 44
                        implicitHeight: 44 * 90.38 / 128
                    }
                    ColumnLayout {
                        spacing: 0
                        StyledText {
                            text: "Aether"
                            font: Tokens.font.title.large
                        }
                        StyledText {
                            text: "Setup"
                            color: Colours.palette.m3onSurfaceVariant
                            font: Tokens.font.label.medium
                        }
                    }
                }

                Repeater {
                    model: win.steps

                    StyledRect {
                        id: item

                        required property var modelData
                        required property int index
                        readonly property bool current: index === win.step
                        readonly property bool reachable: win.phase === "" || win.phase === "failed"

                        Layout.fillWidth: true
                        implicitHeight: layout.implicitHeight + layout.anchors.margins * 2
                        color: current ? Colours.palette.m3secondaryContainer : Colours.layer(Colours.palette.m3surfaceContainerHigh, 2)
                        topLeftRadius: current ? Tokens.rounding.extraLargeIncreased : index === 0 ? Tokens.rounding.extraLarge : Tokens.rounding.extraSmall
                        topRightRadius: topLeftRadius
                        bottomLeftRadius: current ? Tokens.rounding.extraLargeIncreased : index === win.steps.length - 1 ? Tokens.rounding.extraLarge : Tokens.rounding.extraSmall
                        bottomRightRadius: bottomLeftRadius

                        StateLayer {
                            anchors.fill: parent
                            topLeftRadius: parent.topLeftRadius
                            topRightRadius: parent.topRightRadius
                            bottomLeftRadius: parent.bottomLeftRadius
                            bottomRightRadius: parent.bottomRightRadius
                            disabled: !item.reachable
                            onClicked: win.step = item.index
                        }

                        RowLayout {
                            id: layout

                            anchors.fill: parent
                            anchors.margins: Tokens.padding.large
                            spacing: Tokens.spacing.medium

                            StyledRect {
                                Layout.fillHeight: true
                                implicitWidth: height
                                radius: Tokens.rounding.full
                                color: item.current ? Colours.palette.m3primary : Colours.palette.m3secondaryContainer

                                MaterialIcon {
                                    anchors.centerIn: parent
                                    text: item.index < win.step && win.phase !== "" ? "check" : item.modelData.icon
                                    color: item.current ? Colours.palette.m3onPrimary : Colours.palette.m3onSecondaryContainer
                                    fontStyle: Tokens.font.icon.medium
                                    fill: 1
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                StyledText {
                                    Layout.fillWidth: true
                                    text: item.modelData.label
                                    font: Tokens.font.body.medium
                                    elide: Text.ElideRight
                                }
                                StyledText {
                                    Layout.fillWidth: true
                                    text: item.modelData.description
                                    color: Colours.palette.m3onSurfaceVariant
                                    font: Tokens.font.label.small
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }

                StyledText {
                    Layout.fillWidth: true
                    Layout.margins: Tokens.padding.medium
                    wrapMode: Text.WordWrap
                    text: "Caelestia, end-4, DankMaterialShell, Noctalia and Zenith are their authors' work - see NOTICE.txt."
                    color: Colours.palette.m3outline
                    font: Tokens.font.label.small
                }
            }

            // ---- right: the page, like a Caelestia Settings page
            StyledRect {
                id: page

                anchors.left: nav.right
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.margins: Tokens.padding.large
                radius: Tokens.rounding.extraLarge
                color: Colours.tPalette.m3surfaceContainerLow

                StyledText {
                    id: pageTitle

                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: Tokens.padding.extraLarge
                    text: win.steps[win.step].label
                    font: Tokens.font.title.large
                }

                // close
                StyledRect {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Tokens.padding.large
                    implicitWidth: 36
                    implicitHeight: 36
                    radius: Tokens.rounding.full
                    color: "transparent"
                    opacity: win.phase === "copying" || win.phase === "finishing" ? 0.3 : 1

                    StateLayer {
                        anchors.fill: parent
                        radius: parent.radius
                        disabled: win.phase === "copying" || win.phase === "finishing"
                        onClicked: Qt.quit()
                    }
                    MaterialIcon {
                        anchors.centerIn: parent
                        text: "close"
                        color: Colours.palette.m3onSurfaceVariant
                    }
                }

                StyledFlickable {
                    id: flick

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: pageTitle.bottom
                    anchors.bottom: footer.top
                    anchors.margins: Tokens.padding.extraLarge
                    anchors.topMargin: Tokens.spacing.large
                    contentHeight: pages.implicitHeight
                    clip: true

                    Connections {
                        target: win
                        function onStepChanged(): void {
                            flick.contentY = 0;
                        }
                    }

                    Item {
                        id: pages

                        width: flick.width
                        implicitHeight: [p0, p1, p2, p3, p4][win.step].implicitHeight

                        // 0 welcome
                        ColumnLayout {
                            id: p0
                            visible: win.step === 0
                            width: parent.width
                            spacing: Tokens.spacing.extraSmall / 2

                            StyledText {
                                Layout.fillWidth: true
                                Layout.bottomMargin: Tokens.spacing.medium
                                wrapMode: Text.WordWrap
                                text: "Aether runs the best-known Linux desktop shells on Windows. Pick one of them for your whole desktop, or take the bar from one, the launcher from another and the lock screen from a third. Everything follows your wallpaper's colours, the way it does on Linux."
                                color: Colours.palette.m3onSurfaceVariant
                            }
                            StyledText {
                                Layout.fillWidth: true
                                Layout.bottomMargin: Tokens.spacing.medium
                                visible: win.hasSettings
                                wrapMode: Text.WordWrap
                                text: "Aether is already set up for this Windows user - your settings are kept unless you switch that off under Install options."
                                color: Colours.palette.m3primary
                                font: Tokens.font.label.medium
                            }
                            SectionHeader {
                                first: true
                                text: "The shells"
                            }
                            Repeater {
                                model: win.shells
                                ShellRow {
                                    required property var modelData
                                    required property int index
                                    first: index === 0
                                    last: index === win.shells.length - 1
                                    shell: modelData.id
                                    text: modelData.name
                                    subtext: `${modelData.sub}  ·  by ${modelData.by}`
                                }
                            }
                        }

                        // 1 your look
                        RowLayout {
                            id: p1
                            visible: win.step === 1
                            width: parent.width
                            spacing: Tokens.spacing.large

                            ColumnLayout {
                                Layout.preferredWidth: parent.width * 0.44
                                Layout.alignment: Qt.AlignTop
                                spacing: Tokens.spacing.extraSmall / 2

                                SectionHeader {
                                    first: true
                                    text: "Which shell should your desktop look like?"
                                }
                                Repeater {
                                    model: win.shells
                                    ShellRow {
                                        required property var modelData
                                        required property int index
                                        first: index === 0
                                        last: index === win.shells.length - 1
                                        shell: modelData.id
                                        text: modelData.name
                                        subtext: modelData.sub
                                        trailingIcon: win.look === modelData.id ? "check_circle" : "radio_button_unchecked"
                                        onClicked: win.look = modelData.id
                                    }
                                }
                                StyledText {
                                    Layout.fillWidth: true
                                    Layout.topMargin: Tokens.spacing.medium
                                    wrapMode: Text.WordWrap
                                    text: "Next you can swap any part for another shell's."
                                    color: Colours.palette.m3outline
                                    font: Tokens.font.label.small
                                }
                            }

                            Showcase {
                                Layout.fillWidth: true
                                Layout.preferredWidth: parent.width * 0.52
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: Tokens.spacing.large
                                Layout.preferredHeight: width * 9 / 16
                                shell: win.look
                            }
                        }

                        // 2 rice builder
                        RowLayout {
                            id: p2
                            visible: win.step === 2
                            width: parent.width
                            spacing: Tokens.spacing.large

                            ColumnLayout {
                                Layout.preferredWidth: parent.width * 0.5
                                Layout.minimumWidth: 400
                                Layout.alignment: Qt.AlignTop
                                spacing: Tokens.spacing.extraSmall / 2

                                SectionHeader {
                                    first: true
                                    text: "Who draws each part"
                                }
                                Repeater {
                                    model: [
                                        { key: "mixBar", label: "Bar", items: win.allItems },
                                        { key: "mixLauncher", label: "Launcher", items: win.allItems },
                                        { key: "mixNotifs", label: "Notifications", items: win.allItems },
                                        { key: "mixOsd", label: "Volume pop-up", items: win.allItems },
                                        { key: "mixLock", label: "Lock screen", items: win.allItems },
                                        { key: "mixWallpaper", label: "Wallpaper", items: win.wallItems },
                                        { key: "mixDock", label: "Dock", items: win.dockItems },
                                        { key: "mixDesktopWidgets", label: "Desktop widgets", items: win.widgetItems }
                                    ]
                                    SelectRow {
                                        required property var modelData
                                        required property int index
                                        first: index === 0
                                        label: modelData.label
                                        menuItems: modelData.items
                                        active: modelData.items.find(i => i.value === win.mix[modelData.key]) ?? null
                                        onSelected: it => win.setPiece(modelData.key, it.value)
                                        HoverHandler {
                                            onHoveredChanged: preview.hover = hovered ? parent.modelData.key : (preview.hover === parent.modelData.key ? "" : preview.hover)
                                        }
                                    }
                                }
                                SelectRow {
                                    last: true
                                    label: "Title bars on your apps"
                                    menuItems: win.titleItems
                                    active: win.titleItems.find(i => i.value === win.titleBars) ?? null
                                    onSelected: it => win.titleBars = it.value
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredWidth: parent.width * 0.5
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: Tokens.spacing.large
                                spacing: Tokens.spacing.small

                                DesktopPreview {
                                    id: preview
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: width * 9 / 16
                                    TapHandler {
                                        onTapped: win.bigPreview = true
                                    }
                                }
                                StyledText {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: "The bars, launchers, notifications and pop-ups here are cut from each shell's own screenshots. Hover a part to see it; click the picture for full size."
                                    color: Colours.palette.m3outline
                                    font: Tokens.font.label.small
                                }
                            }
                        }

                        // 3 install options
                        ColumnLayout {
                            id: p3
                            visible: win.step === 3
                            width: parent.width
                            spacing: Tokens.spacing.extraSmall / 2

                            SectionHeader {
                                first: true
                                text: "Where"
                            }
                            RowButton {
                                first: true
                                last: true
                                icon: "folder"
                                text: win.target.replace(/\//g, "\\")
                                subtext: "For this Windows user only - no administrator needed"
                                trailingIcon: "edit"
                                onClicked: folderDialog.open()
                            }
                            SectionHeader {
                                text: "Options"
                            }
                            ToggleRow {
                                first: true
                                text: "Start Aether when I sign in"
                                checked: win.autostart
                                onToggled: win.autostart = checked
                            }
                            ToggleRow {
                                text: "Start menu shortcuts"
                                subtext: "Aether Shell, its settings, Uninstall Aether Shell"
                                checked: win.startMenu
                                onToggled: win.startMenu = checked
                            }
                            ToggleRow {
                                text: "Desktop shortcut"
                                checked: win.desktopLink
                                onToggled: win.desktopLink = checked
                            }
                            ToggleRow {
                                text: "Hide the Windows taskbar"
                                subtext: "Your bar takes over; Windows' slides in when the mouse touches the screen edge"
                                checked: win.hideTaskbar
                                onToggled: win.hideTaskbar = checked
                            }
                            ToggleRow {
                                last: !win.hasSettings
                                text: "Battery saver"
                                subtext: "Laptops: pause video wallpapers while on battery"
                                checked: win.batterySaver
                                onToggled: win.batterySaver = checked
                            }
                            ToggleRow {
                                visible: win.hasSettings
                                last: true
                                text: "Keep my current Aether settings"
                                subtext: "Off = start fresh with what you picked here"
                                checked: win.keepSettings
                                onToggled: win.keepSettings = checked
                            }

                            SectionHeader {
                                visible: tilingShipped
                                readonly property bool tilingShipped: Win.FsUtils.classify(`${shellRoot.src}/extras/aether-tiling/LICENSE.md`) === "file"
                                text: "AetherWM - Aether's window manager (tiling + Hyprland config)"
                            }
                            RowButton {
                                visible: Win.FsUtils.classify(`${shellRoot.src}/extras/aether-tiling/LICENSE.md`) === "file"
                                first: true
                                icon: "description"
                                text: "Read AetherWM's license"
                                subtext: "AetherWM is based on komorebi (Komorebi License 2.0): personal use only, shared as source - so it is built on your PC. No Visual Studio needed"
                                trailingIcon: "open_in_new"
                                onClicked: Qt.openUrlExternally(`file:///${shellRoot.src}/extras/aether-tiling/LICENSE.md`)
                            }
                            ToggleRow {
                                visible: Win.FsUtils.classify(`${shellRoot.src}/extras/aether-tiling/LICENSE.md`) === "file"
                                last: true
                                text: "I agree to the license (personal use only) - build AetherWM after installing"
                                subtext: "A window builds it on your PC right after Aether installs (a few minutes; it installs Rust by itself if you have none - no Visual Studio). Then Aether runs it: tiling, Hyprland keys from hyprland.conf (a starter one if you have none), Alt+H/J/K/L"
                                checked: win.tilingAgreed
                                onToggled: win.tilingAgreed = checked
                            }
                        }

                        // 4 install
                        ColumnLayout {
                            id: p4
                            visible: win.step === 4
                            width: parent.width
                            spacing: Tokens.spacing.extraSmall / 2

                            SectionHeader {
                                first: true
                                text: win.phase === "done" ? "Aether is installed" : win.phase === "failed" ? "Something went wrong" : win.phase === "" ? "Ready" : "Installing"
                            }
                            RowButton {
                                first: true
                                icon: "palette"
                                text: `${win.shellName(win.look)} look`
                                subtext: `Bar: ${win.shellName(win.mix.mixBar)} · launcher: ${win.shellName(win.mix.mixLauncher)} · notifications: ${win.shellName(win.mix.mixNotifs)} · lock: ${win.shellName(win.mix.mixLock)}`
                                onClicked: if (win.phase === "") win.step = 2
                            }
                            RowButton {
                                icon: "web_asset"
                                text: "Title bars"
                                subtext: win.titleItems.find(i => i.value === win.titleBars)?.text ?? win.titleBars
                                onClicked: if (win.phase === "") win.step = 2
                            }
                            RowButton {
                                last: true
                                icon: "folder"
                                text: win.target.replace(/\//g, "\\")
                                subtext: win.keepSettings ? "Your current settings are kept" : "Settings start from your picks"
                                onClicked: if (win.phase === "") win.step = 3
                            }

                            StyledText {
                                Layout.fillWidth: true
                                Layout.topMargin: Tokens.spacing.large
                                visible: win.phase !== ""
                                wrapMode: Text.WordWrap
                                text: win.phase === "done" ? "Done. Super+Comma opens Settings any time; Setup and Mix & match are in there."
                                    : win.phase === "failed" ? win.error
                                    : win.phase === "stopping" ? "Closing the Aether that is running..."
                                    : win.phase === "finishing" ? "Installing - this window closes and Aether starts in a few seconds."
                                    : `${Math.min(win.copied, win.totalFiles)} / ${win.totalFiles} files · ${win.lastFile}`
                                color: win.phase === "failed" ? Colours.palette.m3error : Colours.palette.m3onSurfaceVariant
                                elide: Text.ElideMiddle
                            }
                            StyledProgressBar {
                                Layout.fillWidth: true
                                Layout.topMargin: Tokens.spacing.small
                                visible: win.phase !== ""
                                implicitHeight: Tokens.padding.small
                                value: win.phase === "done" ? 1 : win.phase === "finishing" ? 0.97 : Math.min(0.95, win.copied / win.totalFiles)
                                wavy: win.phase === "copying"
                            }
                        }
                    }
                }

                // back / next
                RowLayout {
                    id: footer

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Tokens.padding.extraLarge
                    spacing: Tokens.spacing.small

                    Item {
                        Layout.fillWidth: true
                    }
                    TextButton {
                        visible: win.step > 0 && win.phase === ""
                        type: TextButton.Text
                        text: "Back"
                        onClicked: win.step--
                    }
                    TextButton {
                        visible: win.phase === "" || win.phase === "done" || win.phase === "failed"
                        type: TextButton.Filled
                        horizontalPadding: Tokens.padding.extraLarge
                        text: win.phase === "done" ? "Open Aether" : win.phase === "failed" ? "Try again" : win.step === win.steps.length - 1 ? "Install" : "Next"
                        onClicked: {
                            if (win.phase === "done") {
                                Quickshell.execDetached([`${win.target}/Caelestia.exe`]);
                                Qt.quit();
                            } else if (win.phase === "failed" || win.step === win.steps.length - 1) {
                                win.startInstall();
                            } else {
                                win.step++;
                            }
                        }
                    }
                }
            }

            // full-size preview
            StyledRect {
                anchors.fill: parent
                z: 50
                visible: opacity > 0
                opacity: win.bigPreview ? 1 : 0
                color: Qt.alpha(Colours.palette.m3scrim, 0.85)
                Behavior on opacity {
                    Anim {}
                }
                DesktopPreview {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 60, (parent.height - 60) / 0.5625)
                    height: width * 0.5625
                }
                TapHandler {
                    enabled: win.bigPreview
                    onTapped: win.bigPreview = false
                }
            }
        }

        // menus for the rice builder (Caelestia's split-button menus)
        readonly property list<MenuItem> allItems: [
            MenuItem { text: "Caelestia"; value: "caelestia" },
            MenuItem { text: "end-4"; value: "end4" },
            MenuItem { text: "DankMaterialShell"; value: "dms" },
            MenuItem { text: "Noctalia"; value: "noctalia" },
            MenuItem { text: "Zenith"; value: "zenith" }
        ]
        readonly property list<MenuItem> wallItems: [
            MenuItem { text: "Caelestia"; value: "caelestia" },
            MenuItem { text: "end-4"; value: "end4" },
            MenuItem { text: "DankMaterialShell"; value: "dms" },
            MenuItem { text: "Noctalia"; value: "noctalia" }
        ]
        readonly property list<MenuItem> dockItems: [
            MenuItem { text: "Off"; value: "off" },
            MenuItem { text: "end-4"; value: "end4" },
            MenuItem { text: "DankMaterialShell"; value: "dms" },
            MenuItem { text: "Noctalia"; value: "noctalia" }
        ]
        readonly property list<MenuItem> widgetItems: [
            MenuItem { text: "Off"; value: "off" },
            MenuItem { text: "DankMaterialShell"; value: "dms" },
            MenuItem { text: "Noctalia"; value: "noctalia" }
        ]
        readonly property list<MenuItem> titleItems: [
            MenuItem { text: "As Windows draws them"; value: "system" },
            MenuItem { text: "In your colours"; value: "themed" },
            MenuItem { text: "DankMaterialShell's header"; value: "dms" },
            MenuItem { text: "Discord-style"; value: "discord" },
            MenuItem { text: "KDE Plasma (Breeze)"; value: "kde" },
            MenuItem { text: "Removed (like Hyprland)"; value: "strip" }
        ]
    }

    // ================= pieces =================

    // the shell's own screenshots and showcase video, crossfading
    component Showcase: ClipRect {
        id: sc

        property string shell
        property int at: 0
        readonly property string clipFile: shellRoot.ui.video(shell)
        readonly property var slides: (clipFile ? ["video"] : []).concat(shellRoot.ui.shots(shell))
        onShellChanged: at = 0
        radius: Tokens.rounding.large

        StyledRect {
            anchors.fill: parent
            color: Colours.tPalette.m3surfaceContainer
        }
        Timer {
            running: sc.visible && sc.slides.length > 1
            interval: sc.clipFile && sc.at === 0 ? 16000 : 4000
            repeat: true
            onTriggered: sc.at = (sc.at + 1) % sc.slides.length
        }
        Visual {
            anchors.fill: parent
            visible: sc.clipFile !== ""
            source: sc.clipFile
            playing: sc.visible && sc.clipFile !== "" && sc.at === 0
            opacity: sc.at === 0 ? 1 : 0
            Behavior on opacity {
                Anim {}
            }
        }
        Repeater {
            model: sc.slides
            Image {
                required property string modelData
                required property int index
                anchors.fill: parent
                visible: modelData !== "video"
                source: modelData === "video" ? "" : modelData
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                opacity: index === sc.at ? 1 : 0
                Behavior on opacity {
                    Anim {}
                }
            }
        }
        // dots
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Tokens.padding.medium
            spacing: Tokens.spacing.extraSmall
            Repeater {
                model: sc.slides.length > 1 ? sc.slides.length : 0
                StyledRect {
                    required property int index
                    implicitWidth: index === sc.at ? 18 : 6
                    implicitHeight: 6
                    radius: 3
                    color: index === sc.at ? Colours.palette.m3primary : Qt.alpha(Colours.palette.m3onSurface, 0.5)
                    Behavior on implicitWidth {
                        Anim {}
                    }
                    StateLayer {
                        anchors.fill: parent
                        radius: parent.radius
                        onClicked: sc.at = parent.index
                    }
                }
            }
        }
    }

    // the rice on a desktop, from the real thing: each shell's bar, launcher, notification and pop-up are cut out
    // of that shell's own screenshot (setup-ui/media/<shell>/desk*.jpg, 1920x1080) and placed where they sit on
    // a real screen; what a screenshot does not show is drawn plainly in the scheme's colours
    component DesktopPreview: ClipRect {
        id: dp

        property string hover: ""
        readonly property var m: shellRoot.ui.mix
        readonly property real s: width / 1920
        readonly property var crops: ({
            caelestia: { bar: [{ f: "desk", r: [0, 0, 58, 1080] }], launcher: [{ f: "desk", r: [515, 13, 941, 440] }] },
            dms: { bar: [{ f: "desk", r: [0, 0, 1920, 36] }], osd: [{ f: "desk", r: [864, 1032, 192, 37] }] },
            end4: { bar: [{ f: "desk", r: [0, 0, 1920, 40] }], launcher: [{ f: "desk2", r: [64, 116, 1796, 420] }] },
            noctalia: { bar: [{ f: "desk", r: [0, 0, 1920, 24] }], launcher: [{ f: "desk", r: [672, 27, 576, 537] }], notifs: [{ f: "desk", r: [1629, 33, 285, 81] }] },
            zenith: { bar: [{ f: "desk", r: [8, 5, 95, 28] }, { f: "desk", r: [725, 5, 467, 28] }, { f: "desk", r: [1353, 5, 560, 28] }] }
        })
        function piece(key: string, part: string): var {
            return (crops[m[key]] ?? {})[part] ?? [];
        }
        function lit(key: string): real {
            return hover === "" || hover === key ? 1 : 0.3;
        }
        readonly property real barH: m.mixBar === "caelestia" ? 0 : (piece("mixBar", "bar")[0]?.r[3] ?? 30) * s
        readonly property real barW: m.mixBar === "caelestia" ? 58 * s : 0

        radius: Tokens.rounding.large

        Image {
            anchors.fill: parent
            source: `file:///${shellRoot.src}/config/assets/wallpaper.webp`
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: 1280
        }

        // two app windows with the chosen title bars
        Repeater {
            model: [{ x: 0.17, y: 0.2, w: 0.44, h: 0.5, title: "Discord" }, { x: 0.45, y: 0.34, w: 0.38, h: 0.44, title: "File Explorer" }]
            Rectangle {
                id: appWin
                required property var modelData
                readonly property bool themed: shellRoot.ui.titleBars === "themed" || shellRoot.ui.titleBars === "dms"
                x: dp.barW + dp.width * modelData.x
                y: dp.barH + dp.height * modelData.y
                width: dp.width * modelData.w
                height: dp.height * modelData.h
                radius: 6 * dp.s * 2
                color: Colours.palette.m3surface
                border.width: 1
                border.color: Colours.palette.m3outlineVariant
                opacity: dp.lit("titleBars")
                clip: true
                Rectangle {
                    id: caption
                    visible: shellRoot.ui.titleBars !== "strip"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: Math.max(10, 32 * dp.s)
                    color: appWin.themed ? Colours.palette.m3surfaceContainer : "#f3f3f3"
                    StyledText {
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: appWin.modelData.title
                        color: appWin.themed ? Colours.palette.m3onSurface : "#1b1b1b"
                        font.pixelSize: Math.max(7, parent.height * 0.42)
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        Repeater {
                            model: ["remove", "crop_square", "close"]
                            MaterialIcon {
                                required property string modelData
                                width: caption.height * 1.4
                                horizontalAlignment: Text.AlignHCenter
                                text: modelData
                                color: appWin.themed ? Colours.palette.m3onSurface : "#1b1b1b"
                                fontStyle: Tokens.font.icon.small
                                scale: Math.max(0.3, caption.height / 28)
                            }
                        }
                    }
                }
            }
        }

        // the bar: the real one
        Item {
            anchors.fill: parent
            opacity: dp.lit("mixBar")
            Repeater {
                model: dp.piece("mixBar", "bar")
                Crop {
                    required property var modelData
                    shellId: dp.m.mixBar
                    part: modelData
                    k: dp.s
                    pill: dp.m.mixBar === "zenith"
                }
            }
        }
        // notifications
        Item {
            anchors.fill: parent
            opacity: dp.lit("mixNotifs")
            Repeater {
                model: dp.piece("mixNotifs", "notifs")
                Crop {
                    required property var modelData
                    shellId: dp.m.mixNotifs
                    part: modelData
                    k: dp.s
                }
            }
            StyledRect {
                visible: dp.piece("mixNotifs", "notifs").length === 0
                x: dp.width - width - 8
                y: dp.barH + 6
                width: dp.width * 0.18
                height: dp.height * 0.08
                radius: Tokens.rounding.small
                color: Colours.palette.m3surfaceContainer
                StyledText {
                    anchors.centerIn: parent
                    text: "Notification"
                    color: Colours.palette.m3onSurface
                    font.pixelSize: Math.max(7, parent.height * 0.3)
                }
            }
        }
        // volume pop-up
        Item {
            anchors.fill: parent
            opacity: dp.lit("mixOsd")
            Repeater {
                model: dp.piece("mixOsd", "osd")
                Crop {
                    required property var modelData
                    shellId: dp.m.mixOsd
                    part: modelData
                    k: dp.s
                    dy: dp.m.mixDock !== "off" ? -dp.height * 0.08 : 0
                }
            }
        }
        // dock
        StyledRect {
            visible: dp.m.mixDock !== "off"
            opacity: dp.lit("mixDock")
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            width: dp.width * 0.26
            height: dp.height * 0.065
            radius: Tokens.rounding.medium
            color: Colours.tPalette.m3surfaceContainer
            StyledText {
                anchors.centerIn: parent
                text: `${shellRoot.ui.shellName(dp.m.mixDock)} dock`
                color: Colours.palette.m3onSurface
                font.pixelSize: Math.max(7, parent.height * 0.4)
            }
        }
        // launcher (while its row is hovered)
        Item {
            anchors.fill: parent
            visible: dp.hover === "mixLauncher"
            Repeater {
                model: dp.piece("mixLauncher", "launcher")
                Crop {
                    required property var modelData
                    shellId: dp.m.mixLauncher
                    part: modelData
                    k: dp.s
                }
            }
            StyledRect {
                visible: dp.piece("mixLauncher", "launcher").length === 0
                anchors.centerIn: parent
                width: dp.width * 0.34
                height: dp.height * 0.4
                radius: Tokens.rounding.medium
                color: Colours.palette.m3surfaceContainer
                StyledText {
                    anchors.centerIn: parent
                    text: `${shellRoot.ui.shellName(dp.m.mixLauncher)} launcher`
                    color: Colours.palette.m3onSurface
                }
            }
        }
    }

    // one piece cut from a shell's screenshot, placed where it was on that screen
    component Crop: Item {
        id: cr
        property string shellId
        property var part: ({ f: "desk", r: [0, 0, 0, 0] })
        property real k: 1
        property real dy: 0
        property bool pill: false
        readonly property var r: part.r
        x: r[0] * k
        y: r[1] * k + dy
        width: r[2] * k
        height: r[3] * k
        ClipRect {
            anchors.fill: parent
            radius: cr.pill ? height / 2 : 0
            Image {
                anchors.fill: parent
                source: `file:///${shellRoot.media}/${cr.shellId}/${cr.part.f}.jpg`
                sourceClipRect: Qt.rect(cr.r[0], cr.r[1], cr.r[2], cr.r[3])
                smooth: true
                mipmap: true
                asynchronous: true
            }
        }
    }
}
