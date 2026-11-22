pragma ComponentBehavior: Bound

// Aether picker: ">border" and ">wallpaper" from any shell's launcher open this in the middle of the focused monitor.
// It grows out of a launcher-sized pill with Caelestia's bouncy (expressive spatial) morph, shows what you are about
// to pick, and shrinks back away when you pick or press Esc.
//   border     colour / gradient swatches, a live preview window, "Spin" (the WM turns the gradient)
//   wallpaper  a parallelogram strip: the current one opens wide as the preview; Left / Right, Enter, Esc
// IPC: qs ipc call aetherpicker open border|wallpaper ; close
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Widgets
import Caelestia.Config
import qs.components
import qs.components.containers
import qs.components.controls
import qs.services

Scope {
    id: scope

    property string mode: ""        // "border" | "wallpaper"

    LazyLoader {
        id: loader

        property bool closing

        StyledWindow {
            id: win

            screen: Screens.screens.find(s => Hypr.monitorFor(s) === Hypr.focusedMonitor) ?? Screens.screens[0]
            name: "aether-picker"
            exclusionMode: ExclusionMode.Ignore
            color: "transparent"
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.keyboardFocus: loader.closing ? WlrKeyboardFocus.None : WlrKeyboardFocus.Exclusive
            mask: loader.closing ? emptyRegion : null

            anchors.top: true
            anchors.bottom: true
            anchors.left: true
            anchors.right: true

            Region {
                id: emptyRegion
            }

            Picker {
                anchors.fill: parent
                mode: scope.mode
                closing: loader.closing
                onDone: {
                    loader.closing = true;
                    closeTimer.restart();
                }
            }

            Timer {
                id: closeTimer

                interval: 420
                onTriggered: if (loader.closing) loader.active = false // reopened meanwhile: stay
            }
        }
    }

    function open(m: string): void {
        // your own wallpaper picker (windows.json "wallpaperPicker": "user:<Name>" = user/panels/<Name>.qml, example
        // AetherCoverflow) replaces this one everywhere: ">wallpaper" in any launcher, keys, IPC
        const mine = UserExt.userName(WinSettings.wallpaperPicker);
        if (m === "wallpaper" && mine) {
            UserExt.openPanel(mine);
            return;
        }
        scope.mode = m === "wallpaper" ? "wallpaper" : "border";
        loader.closing = false;
        loader.active = true;
    }

    IpcHandler {
        function open(mode: string): void {
            scope.open(mode);
        }

        function close(): void {
            loader.active = false;
        }

        target: "aetherpicker"
    }

    // ---------------------------------------------------------------- the card
    component Picker: Item {
        id: picker

        required property string mode
        required property bool closing
        signal done

        // the launcher-sized pill it grows out of, and the size it morphs to
        readonly property real pillW: 560
        readonly property real pillH: 64
        readonly property real fullW: mode === "wallpaper" ? Math.min(width * 0.92, 1760) : Math.min(width * 0.6, 900)
        readonly property real fullH: mode === "wallpaper" ? Math.min(height * 0.56, 600) : Math.min(height * 0.62, 520)
        property bool grown: false

        // grow once the window is on screen (a change before its first frame is not animated)
        Timer {
            running: true
            interval: 70
            onTriggered: picker.grown = true
        }
        onClosingChanged: grown = !closing // reopened while closing: grows back from where it is

        Keys.onEscapePressed: picker.done()
        focus: true

        // dim + click outside closes
        Rectangle {
            anchors.fill: parent
            color: "black"
            opacity: picker.grown ? 0.32 : 0
            Behavior on opacity {
                Anim {
                    type: Anim.DefaultEffects
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: picker.done()
            }
        }

        StyledRect {
            id: card

            anchors.centerIn: parent
            width: picker.grown ? picker.fullW : picker.pillW
            height: picker.grown ? picker.fullH : picker.pillH
            opacity: picker.grown || !picker.closing ? 1 : 0
            radius: Tokens.rounding.extraLarge
            color: Colours.tPalette.m3surface
            clip: true

            Behavior on width {
                Anim {
                    type: Anim.DefaultSpatial
                }
            }
            Behavior on height {
                Anim {
                    type: Anim.DefaultSpatial
                }
            }
            Behavior on opacity {
                Anim {
                    type: Anim.FastEffects
                }
            }

            MouseArea {
                anchors.fill: parent // keeps clicks on the card from closing it
            }

            Loader {
                anchors.fill: parent
                anchors.margins: Tokens.padding.extraLarge
                opacity: picker.grown ? 1 : 0
                active: true
                sourceComponent: picker.mode === "wallpaper" ? wallpaperComp : borderComp

                Behavior on opacity {
                    Anim {
                        type: Anim.DefaultEffects
                    }
                }
            }
        }

        // ------------------------------------------------------------ border
        Component {
            id: borderComp

            Item {
                id: bp

                readonly property var swatches: [
                    { name: "Accent", colours: [hex(Colours.palette.m3primary)] },
                    { name: "Accent soft", colours: [hex(Colours.palette.m3secondary)] },
                    { name: "Tertiary", colours: [hex(Colours.palette.m3tertiary)] },
                    { name: "White", colours: ["ffffff"] },
                    { name: "Grey", colours: ["9e9e9e"] },
                    { name: "Red", colours: ["f38ba8"] },
                    { name: "Orange", colours: ["fab387"] },
                    { name: "Yellow", colours: ["f9e2af"] },
                    { name: "Green", colours: ["a6e3a1"] },
                    { name: "Teal", colours: ["94e2d5"] },
                    { name: "Blue", colours: ["89b4fa"] },
                    { name: "Purple", colours: ["cba6f7"] },
                    { name: "Pink", colours: ["f5c2e7"] },
                    { name: "Accent flow", colours: [hex(Colours.palette.m3primary), hex(Colours.palette.m3tertiary)] },
                    { name: "Aurora", colours: ["cba6f7", "89b4fa"] },
                    { name: "Sunset", colours: ["fab387", "f38ba8"] },
                    { name: "Mint", colours: ["a6e3a1", "94e2d5"] },
                    { name: "Ocean", colours: ["89b4fa", "94e2d5"] },
                    { name: "Candy", colours: ["f5c2e7", "cba6f7"] },
                    { name: "Neon", colours: ["33ccff", "00ff99"] },
                    { name: "Ember", colours: ["f9e2af", "fab387", "f38ba8"] },
                    { name: "Spectrum", colours: ["f38ba8", "f9e2af", "a6e3a1", "89b4fa", "cba6f7"] }
                ]
                property int current: 0
                property bool spin: (HyprConf.opt("aether:border_spin_ms", "0") | 0) > 0
                readonly property var sel: swatches[current]

                function hex(c: color): string {
                    const h = v => Math.round(v * 255).toString(16).padStart(2, "0");
                    return h(c.r) + h(c.g) + h(c.b);
                }

                function pick(): void {
                    const cols = bp.sel.colours;
                    const value = cols.length > 1 ? cols.map(c => `rgba(${c}ff)`).join(" ") + " 45deg" : `rgb(${cols[0]})`;
                    HyprConf.setOption("general:col.active_border", value);
                    HyprConf.setOption("aether:border_spin_ms", bp.spin && cols.length > 1 ? "3000" : "0");
                    const ctl = (Quickshell.env("CAELESTIA_PACKAGE_DIR") || Quickshell.shellPath("..")).replace(/\\/g, "/") + "/qs/hyprctl.exe";
                    Quickshell.execDetached([ctl, "keyword", "general:col.active_border", value]);
                    Quickshell.execDetached([ctl, "keyword", "aether:border_spin_ms", bp.spin && cols.length > 1 ? "3000" : "0"]);
                    picker.done();
                }

                Keys.onLeftPressed: current = Math.max(0, current - 1)
                Keys.onRightPressed: current = Math.min(swatches.length - 1, current + 1)
                Keys.onUpPressed: current = Math.max(0, current - grid.columns)
                Keys.onDownPressed: current = Math.min(swatches.length - 1, current + grid.columns)
                Keys.onReturnPressed: pick()
                Keys.onSpacePressed: spin = !spin
                Keys.onEscapePressed: picker.done()
                focus: true
                Component.onCompleted: forceActiveFocus()

                // preview: a window with that border
                Item {
                    id: preview

                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * 0.42

                    StyledText {
                        id: title

                        text: bp.sel.name
                        font: Tokens.font.title.medium
                    }

                    StyledText {
                        anchors.top: title.bottom
                        text: bp.sel.colours.map(c => "#" + c).join("  →  ")
                        color: Colours.palette.m3outline
                        font: Tokens.font.body.small
                    }

                    ClippingRectangle {
                        id: frame

                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: 20
                        width: parent.width * 0.86
                        height: width * 0.66
                        radius: 14
                        color: "transparent"

                        // the border: a gradient spinning under the window
                        Rectangle {
                            anchors.centerIn: parent
                            width: Math.hypot(frame.width, frame.height) * 1.1
                            height: width
                            rotation: 45
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: "#" + bp.sel.colours[0] }
                                GradientStop { position: 0.5; color: "#" + bp.sel.colours[Math.floor((bp.sel.colours.length - 1) / 2)] }
                                GradientStop { position: 1; color: "#" + bp.sel.colours[bp.sel.colours.length - 1] }
                            }
                            RotationAnimator on rotation {
                                running: bp.spin && bp.sel.colours.length > 1
                                from: 45
                                to: 405
                                duration: 3000
                                loops: Animation.Infinite
                            }
                        }

                        // the window
                        StyledRect {
                            anchors.fill: parent
                            anchors.margins: 4
                            radius: 10
                            color: Colours.tPalette.m3surfaceContainer

                            Row {
                                x: 12
                                y: 10
                                spacing: 6
                                Repeater {
                                    model: 3
                                    Rectangle {
                                        width: 10
                                        height: 10
                                        radius: 5
                                        color: Colours.palette.m3outlineVariant
                                    }
                                }
                            }
                            Column {
                                x: 16
                                y: 36
                                spacing: 8
                                Repeater {
                                    model: 4
                                    Rectangle {
                                        required property int index
                                        width: (frame.width - 60) * (index % 2 ? 0.55 : 0.8)
                                        height: 8
                                        radius: 4
                                        color: Colours.palette.m3outlineVariant
                                        opacity: 0.6
                                    }
                                }
                            }
                        }
                    }

                    Row {
                        anchors.bottom: parent.bottom
                        spacing: 10
                        StyledSwitch {
                            checked: bp.spin
                            enabled: bp.sel.colours.length > 1
                            onToggled: bp.spin = checked
                        }
                        StyledText {
                            anchors.verticalCenter: parent.verticalCenter
                            text: bp.sel.colours.length > 1 ? qsTr("Spin the gradient (Space)") : qsTr("Spin works with gradients")
                            color: Colours.palette.m3onSurfaceVariant
                            font: Tokens.font.body.small
                        }
                    }
                }

                // swatches
                Grid {
                    id: grid

                    anchors.right: parent.right
                    anchors.top: parent.top
                    width: parent.width * 0.54
                    columns: 5
                    spacing: 12

                    Repeater {
                        model: bp.swatches

                        StyledRect {
                            id: sw

                            required property var modelData
                            required property int index

                            width: (grid.width - grid.spacing * (grid.columns - 1)) / grid.columns
                            height: width * 0.62
                            radius: Tokens.rounding.medium
                            color: "#" + modelData.colours[0]
                            scale: bp.current === index ? 1.08 : 1
                            opacity: 0
                            Component.onCompleted: swIn.start()

                            gradient: modelData.colours.length > 1 ? swGrad : null
                            Gradient {
                                id: swGrad
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: "#" + sw.modelData.colours[0] }
                                GradientStop { position: 1; color: "#" + sw.modelData.colours[sw.modelData.colours.length - 1] }
                            }

                            Behavior on scale {
                                Anim {
                                    type: Anim.FastSpatial
                                }
                            }

                            SequentialAnimation {
                                id: swIn
                                PauseAnimation {
                                    duration: 120 + sw.index * 18
                                }
                                Anim {
                                    target: sw
                                    property: "opacity"
                                    to: 1
                                    type: Anim.FastEffects
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                color: "transparent"
                                border.width: bp.current === sw.index ? 3 : 0
                                border.color: Colours.palette.m3onSurface
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onEntered: bp.current = sw.index
                                onClicked: bp.pick()
                            }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------ wallpaper
        Component {
            id: wallpaperComp

            Item {
                id: wp

                readonly property real slant: 0.18      // the parallelogram lean
                readonly property real narrow: height * 0.36
                readonly property real wide: Math.min(width * 0.5, height * 16 / 9 * 0.86)

                function pick(): void {
                    const e = Wallpapers.list[list.currentIndex];
                    if (e)
                        Wallpapers.setWallpaper(e.path);
                    picker.done();
                }

                Keys.onLeftPressed: list.decrementCurrentIndex()
                Keys.onRightPressed: list.incrementCurrentIndex()
                Keys.onReturnPressed: pick()
                Keys.onEscapePressed: picker.done()
                focus: true
                Component.onCompleted: {
                    const norm = x => String(x).split(String.fromCharCode(92)).join("/").toLowerCase(); // / or  paths
                    const i = Wallpapers.list.findIndex(e => norm(e.path) === norm(Wallpapers.actualCurrent));
                    list.currentIndex = Math.max(0, i);
                    list.positionViewAtIndex(list.currentIndex, ListView.Center);
                    Qt.callLater(() => list.positionViewAtIndex(list.currentIndex, ListView.Center)); // after the widths settle
                    forceActiveFocus();
                }

                ListView {
                    id: list

                    anchors.fill: parent
                    anchors.bottomMargin: 34
                    orientation: ListView.Horizontal
                    spacing: -height * wp.slant + 10       // leaning cards overlap their neighbours' slant
                    model: Wallpapers.list
                    highlightRangeMode: ListView.StrictlyEnforceRange
                    preferredHighlightBegin: (width - wp.wide) / 2
                    preferredHighlightEnd: (width + wp.wide) / 2
                    highlightMoveDuration: 380
                    cacheBuffer: 600
                    boundsBehavior: Flickable.StopAtBounds
                    // room before the first and after the last card, so any card (the current one too) sits in the middle
                    header: Item {
                        width: (list.width - wp.wide) / 2
                        height: 1
                    }
                    footer: Item {
                        width: (list.width - wp.wide) / 2
                        height: 1
                    }

                    WheelHandler {
                        onWheel: e => e.angleDelta.y < 0 ? list.incrementCurrentIndex() : list.decrementCurrentIndex()
                    }

                    delegate: Item {
                        id: cardItem

                        required property var modelData
                        required property int index
                        readonly property bool isCurrent: ListView.isCurrentItem

                        width: isCurrent ? wp.wide : wp.narrow
                        height: list.height
                        z: isCurrent ? 2 : 1
                        opacity: 0
                        Component.onCompleted: cardIn.start()

                        Behavior on width {
                            Anim {
                                type: Anim.DefaultSpatial
                            }
                        }

                        SequentialAnimation {
                            id: cardIn
                            PauseAnimation {
                                duration: 160 + Math.abs(cardItem.index - list.currentIndex) * 45
                            }
                            ParallelAnimation {
                                Anim {
                                    target: cardItem
                                    property: "opacity"
                                    to: 1
                                    type: Anim.DefaultEffects
                                }
                                Anim {
                                    target: shift
                                    property: "y"
                                    from: 60
                                    to: 0
                                    type: Anim.DefaultSpatial
                                }
                            }
                        }

                        // the parallelogram: a sheared clip, the picture sheared back so it stays straight
                        ClippingRectangle {
                            id: para

                            anchors.fill: parent
                            radius: 18
                            color: Colours.tPalette.m3surfaceContainer
                            transform: [
                                Translate {
                                    id: shift
                                },
                                Matrix4x4 {
                                    matrix: Qt.matrix4x4(1, -wp.slant, 0, wp.slant * para.height / 2, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
                                }
                            ]

                            Image {
                                anchors.centerIn: parent
                                width: parent.width + parent.height * wp.slant
                                height: parent.height
                                source: cardItem.modelData.path
                                sourceSize.width: 900
                                asynchronous: true
                                fillMode: Image.PreserveAspectCrop
                                transform: Matrix4x4 {
                                    matrix: Qt.matrix4x4(1, wp.slant, 0, -wp.slant * para.height / 2, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                color: "black"
                                opacity: cardItem.isCurrent ? 0 : 0.35
                                Behavior on opacity {
                                    Anim {
                                        type: Anim.DefaultEffects
                                    }
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                color: "transparent"
                                border.width: cardItem.isCurrent ? 3 : 0
                                border.color: Colours.palette.m3primary
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: cardItem.isCurrent ? wp.pick() : list.currentIndex = cardItem.index
                        }
                    }
                }

                StyledText {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (Wallpapers.list[list.currentIndex]?.relativePath ?? "") + "   ·   ← → choose   Enter set   Esc close"
                    color: Colours.palette.m3onSurfaceVariant
                    font: Tokens.font.body.small
                }
            }
        }
    }
}
