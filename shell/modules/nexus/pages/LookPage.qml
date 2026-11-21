pragma ComponentBehavior: Bound

// Settings -> Look & feel: size of everything, the bar, animations, wallpaper transition and picker,
// desktop extras, the Settings window background and the loading screen. Saves straight away.
import QtQuick
import QtQuick.Layouts
import Quickshell
import Caelestia.Config
import Caelestia.I18n
import qs.components
import qs.services
import qs.utils
import qs.modules.nexus.common

PageBase {
    id: root

    title: Tr.tr("Look & feel")

    readonly property list<string> transitions: ["Chromatic bloom", "Colour distance", "Crossfade", "Crosshatch", "Crosswarp", "Directional",
        "Directional wipe", "Fade through colour", "Glitch", "Glitch displace", "Heat melt", "Ink splash", "Inkwell drop", "Iris", "Morph",
        "Parametric glitch", "Perlin", "Pixelate", "Pixel fade wave", "Plasma flow", "Polka dots curtain", "Puzzle", "Random squares", "Smoke",
        "Soft warp fade", "Voronoi shatter", "Zoom blur pull"]

    function setScale(v: real): void {
        const a = GlobalConfig.appearance;
        for (const n of [a.rounding, a.spacing, a.padding, a.font])
            if (n && n.scale !== undefined) n.scale = v;
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: root.cappedWidth
        spacing: Tokens.spacing.extraSmall / 2

        // ---------------- size ----------------
        SectionHeader {
            first: true
            text: Tr.tr("Size")
        }

        StepperRow {
            first: true
            label: Tr.tr("UI scale")
            subtext: Tr.tr("Text, spacing, padding and rounding together (%)")
            value: Math.round((GlobalConfig.appearance.font.scale ?? 1) * 100)
            from: 50
            to: 250
            stepSize: 5
            onMoved: v => root.setScale(v / 100)
        }

        StepperRow {
            label: Tr.tr("Text size")
            subtext: Tr.tr("Only the text (%)")
            value: Math.round((GlobalConfig.appearance.font.scale ?? 1) * 100)
            from: 50
            to: 250
            stepSize: 5
            onMoved: v => GlobalConfig.appearance.font.scale = v / 100
        }

        StepperRow {
            label: Tr.tr("Roundness")
            subtext: Tr.tr("0 = square corners (%)")
            value: Math.round(GlobalConfig.appearance.rounding.scale * 100)
            from: 0
            to: 300
            stepSize: 10
            onMoved: v => GlobalConfig.appearance.rounding.scale = v / 100
        }

        StepperRow {
            label: Tr.tr("Spacing")
            subtext: Tr.tr("Room between things (%)")
            value: Math.round(GlobalConfig.appearance.spacing.scale * 100)
            from: 25
            to: 300
            stepSize: 10
            onMoved: v => GlobalConfig.appearance.spacing.scale = v / 100
        }

        StepperRow {
            last: true
            label: Tr.tr("Bar width")
            subtext: Tr.tr("Caelestia's side bar, in pixels")
            value: TokenConfig.sizes.bar.innerWidth
            from: 24
            to: 96
            stepSize: 2
            onMoved: v => TokenConfig.sizes.bar.innerWidth = v
        }

        // ---------------- motion ----------------
        SectionHeader {
            text: Tr.tr("Motion")
        }

        StepperRow {
            first: true
            last: true
            label: Tr.tr("Animation length")
            subtext: Tr.tr("100 = normal, lower = snappier, 0 = no animations (%)")
            value: Math.round((GlobalConfig.appearance.anim.durations.scale ?? 1) * 100)
            from: 0
            to: 300
            stepSize: 10
            onMoved: v => GlobalConfig.appearance.anim.durations.scale = v / 100
        }

        // ---------------- wallpaper ----------------
        SectionHeader {
            text: Tr.tr("Wallpaper change effect")
        }

        ToggleRow {
            first: true
            text: Tr.tr("A different effect every time")
            checked: WinSettings.randomTransition
            onToggled: WinSettings.randomTransition = checked
        }

        Flow {
            Layout.fillWidth: true
            Layout.topMargin: Tokens.spacing.small
            Layout.bottomMargin: Tokens.spacing.small
            spacing: Tokens.spacing.small
            opacity: WinSettings.randomTransition ? 0.45 : 1

            Repeater {
                model: root.transitions

                StyledRect {
                    id: chip

                    required property string modelData
                    required property int index
                    readonly property bool on: GlobalConfig.background.wallpaperTransition === index

                    implicitWidth: chipText.implicitWidth + Tokens.padding.large * 2
                    implicitHeight: chipText.implicitHeight + Tokens.padding.small * 2
                    radius: Tokens.rounding.full
                    color: on ? Colours.palette.m3secondaryContainer : Colours.tPalette.m3surfaceContainer

                    StyledText {
                        id: chipText
                        anchors.centerIn: parent
                        text: chip.modelData
                        color: chip.on ? Colours.palette.m3onSecondaryContainer : Colours.palette.m3onSurfaceVariant
                    }

                    StateLayer {
                        radius: parent.radius
                        onClicked: GlobalConfig.background.wallpaperTransition = chip.index
                    }
                }
            }
        }

        // ---------------- wallpaper picker ----------------
        SectionHeader {
            text: Tr.tr("Wallpaper picker")
        }

        StepperRow {
            first: true
            label: Tr.tr("Wallpapers shown")
            value: GlobalConfig.launcher.maxWallpapers
            from: 3
            to: 25
            onMoved: v => GlobalConfig.launcher.maxWallpapers = v
        }

        StepperRow {
            label: Tr.tr("Thumbnail width")
            value: TokenConfig.sizes.launcher.wallpaperWidth
            from: 120
            to: 600
            stepSize: 10
            onMoved: v => TokenConfig.sizes.launcher.wallpaperWidth = v
        }

        StepperRow {
            last: true
            label: Tr.tr("Thumbnail height")
            value: TokenConfig.sizes.launcher.wallpaperHeight
            from: 80
            to: 400
            stepSize: 10
            onMoved: v => TokenConfig.sizes.launcher.wallpaperHeight = v
        }

        // ---------------- desktop ----------------
        SectionHeader {
            text: Tr.tr("Desktop")
        }

        ToggleRow {
            first: true
            text: Tr.tr("Clock on the desktop")
            checked: GlobalConfig.background.desktopClock.enabled
            onToggled: GlobalConfig.background.desktopClock.enabled = checked
        }

        ToggleRow {
            last: true
            text: Tr.tr("Music visualiser on the desktop")
            subtext: Tr.tr("Bars that move with what you are playing")
            checked: GlobalConfig.background.visualiser.enabled
            onToggled: GlobalConfig.background.visualiser.enabled = checked
        }

        // ---------------- settings background ----------------
        SectionHeader {
            text: Tr.tr("Settings window background")
        }

        Repeater {
            model: [
                { id: "video", icon: "movie", name: Tr.tr("Video"), sub: Tr.tr("Your video behind Settings (windows.json riceBackground), else the wallpaper") },
                { id: "aurora", icon: "blur_on", name: Tr.tr("Aurora"), sub: Tr.tr("Soft colour clouds drifting") },
                { id: "particles", icon: "grain", name: Tr.tr("Particles"), sub: Tr.tr("Small glowing dots floating up") },
                { id: "waves", icon: "waves", name: Tr.tr("Waves"), sub: Tr.tr("Slow, soft colour bands") },
                { id: "none", icon: "block", name: Tr.tr("None"), sub: "" }
            ]

            RowButton {
                required property var modelData
                required property int index

                first: index === 0
                icon: modelData.icon
                text: modelData.name
                subtext: modelData.sub
                trailingIcon: WinSettings.nexusFx === modelData.id ? "check_circle" : "radio_button_unchecked"
                onClicked: WinSettings.nexusFx = modelData.id
            }
        }

        StepperRow {
            label: Tr.tr("Effect strength")
            value: Math.round(WinSettings.nexusFxStrength * 100)
            from: 0
            to: 100
            stepSize: 5
            onMoved: v => WinSettings.nexusFxStrength = v / 100
        }

        StepperRow {
            last: true
            label: Tr.tr("Effect speed")
            subtext: Tr.tr("100 = normal (%)")
            value: Math.round(WinSettings.nexusFxSpeed * 100)
            from: 25
            to: 300
            stepSize: 25
            onMoved: v => WinSettings.nexusFxSpeed = v / 100
        }

        // ---------------- loading screen ----------------
        SectionHeader {
            text: Tr.tr("Loading screen")
        }

        ToggleRow {
            first: true
            text: Tr.tr("Show a loading screen when Caelestia starts")
            checked: WinSettings.splashEnabled
            onToggled: WinSettings.splashEnabled = checked
        }

        Repeater {
            model: [
                { id: "dots", icon: "more_horiz", name: Tr.tr("Colour dots") },
                { id: "logo", icon: "auto_awesome", name: Tr.tr("Caelestia logo") },
                { id: "minimal", icon: "horizontal_rule", name: Tr.tr("Minimal line") }
            ]

            RowButton {
                required property var modelData

                icon: modelData.icon
                text: modelData.name
                trailingIcon: WinSettings.splashStyle === modelData.id ? "check_circle" : "radio_button_unchecked"
                onClicked: WinSettings.splashStyle = modelData.id
            }
        }

        StepperRow {
            label: Tr.tr("How long (ms)")
            value: WinSettings.splashMs
            from: 300
            to: 6000
            stepSize: 100
            onMoved: v => WinSettings.splashMs = v
        }

        TextFieldRow {
            label: Tr.tr("Text")
            subtext: Tr.tr("Empty = no text")
            value: WinSettings.splashText
            onEditingFinished: v => WinSettings.splashText = v
        }

        TextFieldRow {
            label: Tr.tr("Dot colours")
            subtext: Tr.tr("Comma-separated, e.g. #f5c2e7, #89b4fa - empty = your scheme's colours")
            value: WinSettings.splashColours.join(", ")
            onEditingFinished: v => WinSettings.splashColours = v.split(",").map(s => s.trim()).filter(s => s.length)
        }

        RowButton {
            last: true
            icon: "play_arrow"
            text: Tr.tr("Preview the loading screen")
            onClicked: SplashState.show()
        }
    }
}
