import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland
import Quickshell.Io
import "components"
import "../" as Shell
import "../services"

PanelWindow {
    id: overlayRoot
    visible: DynamicIslandService.active
    color: "transparent"

    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    WlrLayershell.namespace: "zenith-dynamic-island-overlay"

    anchors {
        top: true
        bottom: true
        left: true
        right: true
    }

    // Dismissal here is a click catcher, not a focus grab.
    //
    // A HyprlandFocusGrab is wrong for this window. Bar.qml already takes
    // exclusive keyboard focus while the island is open --
    //     keyboardFocus: DynamicIslandService.active ? Exclusive : None
    // -- because the search field the user types into lives in the bar, not
    // here. Engaging a grab took focus routing away from the bar a moment after
    // opening, so the launcher accepted a keystroke or two and then went deaf.
    //
    // Instead the window drops its input mask and catches clicks itself: the
    // card swallows its own, anything outside it closes. That is the ordinary
    // launcher behaviour and it does not touch focus at all.
    // The backdrop that does this already exists further down, declared before
    // mainCard so the card stacks above it. It never fired because of the mask.

    Shortcut {
        sequences: ["Escape"]
        enabled: overlayRoot.visible
        onActivated: DynamicIslandService.close()
    }

    // No mask on purpose: the window has to receive clicks outside the card in
    // order to dismiss on them. A mask limited input to the card, which is why
    // outside clicks reached the desktop instead of closing the launcher.


    // This window is deliberately NOT registered with MenuService.
    //
    // Its visibility is a binding -- `visible: DynamicIslandService.active`
    // in shell.qml -- and that binding is the only thing that ever shows it.
    // MenuService.closeAll() closes registered menus by assigning
    // `menu.visible = false`, and in QML an imperative assignment
    // permanently replaces the binding underneath it. So the first time
    // anything called closeAll() -- the `close` keybind, clicking outside a
    // menu, or entering fullscreen -- this window's binding was destroyed and
    // the launcher, clipboard and emoji picker could never open again for the
    // rest of the session. The service still flipped to active (the dismiss
    // overlay kept appearing), but no window was left listening.
    //
    // Registering bought nothing anyway: DismissOverlay, the only reader of
    // MenuService.openMenus, already tests DynamicIslandService.active
    // separately. Closing is handled by DynamicIslandService.close(), which
    // closeAll() calls directly.
    onVisibleChanged: {
        if (visible) {
            showAnim.restart();
        } else {
            mainCard.opacity = 0;
            mainCard.scale = 0.95;
            mainCard.y = Shell.Theme.scaled(6);
        }
    }


    // Fast, buttery-smooth entrance animation
    ParallelAnimation {
        id: showAnim
        NumberAnimation {
            target: mainCard
            property: "opacity"
            from: 0
            to: 1
            duration: Shell.Theme.animFast
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: mainCard
            property: "scale"
            from: 0.94
            to: 1.0
            duration: Shell.Theme.animFast
            easing.type: Easing.OutBack
            easing.overshoot: 1.04
        }
        NumberAnimation {
            target: mainCard
            property: "y"
            from: -4
            to: Shell.Theme.scaled(6)
            duration: Shell.Theme.animFast
            easing.type: Easing.OutCubic
        }
    }

    // Dismiss overlay backdrop click
    MouseArea {
        id: backdropMouseArea
        anchors.fill: parent
        onClicked: DynamicIslandService.close()
    }


    // Centered Results Container below the Top Bar Center Widget
    Item {
        id: mainCard

        // Swallows clicks that land on the card padding but miss a control.
        // With z: -1 it sits underneath child controls so it never blocks them.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            z: -1
            onPressed: (mouse) => mouse.accepted = true
        }

        anchors.horizontalCenter: parent.horizontalCenter
        y: Shell.Theme.scaled(6)
        width: DynamicIslandService.activeMode === "launcher" ? Shell.Theme.scaled(480) : Shell.Theme.scaled(593)
        Behavior on width { NumberAnimation { duration: Shell.Theme.animFast; easing.type: Easing.OutCubic } }
        height: {
            if (DynamicIslandService.activeMode === "launcher") return launcherResultsView.height;
            if (DynamicIslandService.activeMode === "clipboard") return clipboardResultsView.height;
            if (DynamicIslandService.activeMode === "emoji") return emojiResultsView.height;
            return 0;
        }
        opacity: 0
        scale: 0.95

        Column {
            id: cardColumn
            width: parent.width
            height: parent.height
            spacing: Shell.Theme.scaled(8)

            // --- LAUNCHER RESULTS VIEW ---
            Rectangle {
                id: launcherResultsView
                width: parent.width
                height: Math.max(54, DynamicIslandService.displayedApps.length * 52 + 12)
                implicitHeight: height
                radius: Shell.Theme.scaled(14)
                visible: DynamicIslandService.activeMode === "launcher"
                color: Shell.Theme.glassBackground
                border.color: Shell.Theme.glassBorder
                border.width: 1

                ListView {
                    id: appListView
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    model: DynamicIslandService.displayedApps
                    spacing: 4
                    interactive: false
                    currentIndex: DynamicIslandService.selectedIndex

                    visible: DynamicIslandService.displayedApps.length > 0

                    delegate: Rectangle {
                        id: appDelegate
                        width: appListView.width
                        height: 46
                        radius: Shell.Theme.scaled(10)

                        required property var modelData
                        required property int index

                        readonly property bool isSelected: index === DynamicIslandService.selectedIndex

                        color: isSelected
                            ? Qt.rgba(Shell.Theme.accentColor.r, Shell.Theme.accentColor.g, Shell.Theme.accentColor.b, 0.20)
                            : (hoverArea.containsMouse ? Shell.Theme.cardBackgroundHover : "transparent")

                        border.color: isSelected
                            ? Shell.Theme.accentColor
                            : "transparent"
                        scale: hoverArea.pressed ? 0.96 : 1.0

                        Behavior on scale { NumberAnimation { duration: Shell.Theme.animInstant; easing.type: Easing.OutCubic } }
                        Behavior on color { ColorAnimation { duration: Shell.Theme.animInstant } }
                        Behavior on border.color { ColorAnimation { duration: Shell.Theme.animInstant } }

                        MouseArea {
                            id: hoverArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            onEntered: DynamicIslandService.selectedIndex = index
                            onClicked: DynamicIslandService.launchApp(appDelegate.modelData)
                        }

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            spacing: 12

                            // Math, Web, Terminal, or App Icon
                            Item {
                                width: 28
                                height: 28
                                anchors.verticalCenter: parent.verticalCenter

                                Rectangle {
                                    anchors.fill: parent
                                    radius: Shell.Theme.scaled(7)
                                    visible: !!(appDelegate.modelData && (appDelegate.modelData.isMath || appDelegate.modelData.isTerminal))
                                    color: (appDelegate.modelData && appDelegate.modelData.isMath) ? Qt.rgba(Shell.Theme.accentColor.r, Shell.Theme.accentColor.g, Shell.Theme.accentColor.b, 0.2) : Qt.rgba(255, 255, 255, 0.1)
                                    border.width: 0

                                    Text {
                                        anchors.centerIn: parent
                                        text: (appDelegate.modelData && appDelegate.modelData.isMath) ? "󰪚" : "󰆍"
                                        font.family: Shell.Theme.iconFont
                                        font.pixelSize: Shell.Theme.scaled(20)
                                        color: (appDelegate.modelData && appDelegate.modelData.isMath) ? Shell.Theme.accentColor : Shell.Theme.text
                                    }
                                }

                                Loader {
                                    anchors.fill: parent
                                    active: !!(appDelegate.modelData && !appDelegate.modelData.isMath && !appDelegate.modelData.isTerminal)
                                    sourceComponent: IconImage {
                                        appName: (appDelegate.modelData && appDelegate.modelData.name) ? appDelegate.modelData.name : ""
                                        desktopEntry: (appDelegate.modelData && appDelegate.modelData.id) ? appDelegate.modelData.id : ""
                                        iconName: (appDelegate.modelData && appDelegate.modelData.icon) ? appDelegate.modelData.icon : ""
                                    }
                                }
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 38
                                spacing: 1

                                Text {
                                    text: (appDelegate.modelData && appDelegate.modelData.name) ? appDelegate.modelData.name : "Unknown"
                                    color: appDelegate.isSelected ? Shell.Theme.accentColor : Shell.Theme.text
                                    font.pixelSize: Shell.Theme.scaled(13)
                                    font.weight: appDelegate.isSelected ? Font.Bold : Font.Medium
                                    elide: Text.ElideRight
                                    width: parent.width
                                }

                                Text {
                                    text: (appDelegate.modelData) ? (appDelegate.modelData.genericName || appDelegate.modelData.comment || "") : ""
                                    color: Shell.Theme.subtext0
                                    font.pixelSize: Shell.Theme.scaled(11)
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                    width: parent.width
                                    visible: text !== ""
                                }
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: DynamicIslandService.displayedApps.length === 0
                    text: DynamicIslandService.query.trim() === "" ? "Type to search applications..." : "No applications found"
                    color: Shell.Theme.subtext0
                    font.pixelSize: Shell.Theme.scaled(13)
                }
            }

            // --- CLIPBOARD RESULTS VIEW ---
            Rectangle {
                id: clipboardResultsView
                width: parent.width
                height: Math.min(360, Math.max(54, DynamicIslandService.displayedClips.length * 48 + 12))
                implicitHeight: height
                radius: Shell.Theme.scaled(14)
                visible: DynamicIslandService.activeMode === "clipboard"
                color: Shell.Theme.glassBackground
                border.color: Shell.Theme.glassBorder
                border.width: 1

                ListView {
                    id: clipListView
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    model: DynamicIslandService.displayedClips
                    spacing: 4
                    currentIndex: DynamicIslandService.selectedIndex

                    visible: DynamicIslandService.displayedClips.length > 0

                    delegate: Rectangle {
                        id: clipDelegate
                        width: clipListView.width
                        height: 44
                        radius: Shell.Theme.scaled(10)

                        required property var modelData
                        required property int index

                        readonly property bool isSelected: index === DynamicIslandService.selectedIndex
                        readonly property bool isDeleteFocused: isSelected && (delMouse.containsMouse || DynamicIslandService.deleteFocused)

                        color: isSelected
                            ? Qt.rgba(Shell.Theme.accentColor.r, Shell.Theme.accentColor.g, Shell.Theme.accentColor.b, 0.20)
                            : ((rowMouse.containsMouse || delMouse.containsMouse) ? Shell.Theme.cardBackgroundHover : "transparent")

                        border.color: isSelected
                            ? Shell.Theme.accentColor
                            : "transparent"
                        scale: (rowMouse.pressed || delMouse.pressed) ? 0.98 : 1.0

                        Behavior on scale { NumberAnimation { duration: Shell.Theme.animInstant; easing.type: Easing.OutCubic } }
                        Behavior on color { ColorAnimation { duration: Shell.Theme.animInstant } }
                        Behavior on border.color { ColorAnimation { duration: Shell.Theme.animInstant } }

                        // 1. Primary row click & hover area (Copies clip to clipboard, stops before deleteBtn)
                        MouseArea {
                            id: rowMouse
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.right: deleteBtn.left
                            anchors.rightMargin: Shell.Theme.scaled(4)
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            onEntered: {
                                DynamicIslandService.selectedIndex = index;
                                DynamicIslandService.deleteFocused = false;
                            }
                            onClicked: {
                                let item = DynamicIslandService.displayedClips[index] || clipDelegate.modelData;
                                DynamicIslandService.copyClipItem(item);
                            }
                        }

                        // 2. Row Content: Icon & Preview Text
                        RowLayout {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.right: deleteBtn.left
                            anchors.rightMargin: Shell.Theme.scaled(8)
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10

                            Text {
                                text: "󰅍"
                                font.family: Shell.Theme.iconFont
                                font.pixelSize: Shell.Theme.scaled(20)
                                color: clipDelegate.isSelected ? Shell.Theme.accentColor : Shell.Theme.subtext0
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Text {
                                text: (clipDelegate.modelData && clipDelegate.modelData.preview) ? clipDelegate.modelData.preview : ""
                                color: clipDelegate.isSelected ? Shell.Theme.text : Shell.Theme.subtext0
                                font.pixelSize: Shell.Theme.scaled(12)
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }

                        // 3. Dedicated Delete Bin Button (Shows focus pill on hover/focus, dulls text icon)
                        Rectangle {
                            id: deleteBtn
                            anchors.right: parent.right
                            anchors.rightMargin: Shell.Theme.scaled(6)
                            anchors.verticalCenter: parent.verticalCenter
                            width: Shell.Theme.scaled(32)
                            height: Shell.Theme.scaled(32)
                            radius: Shell.Theme.scaled(8)
                            z: 20

                            color: clipDelegate.isDeleteFocused
                                ? Qt.rgba(255, 255, 255, 0.16)
                                : "transparent"
                            border.color: clipDelegate.isDeleteFocused
                                ? Qt.rgba(255, 255, 255, 0.32)
                                : "transparent"
                            border.width: 1

                            scale: delMouse.pressed ? 0.90 : (clipDelegate.isDeleteFocused ? 1.05 : 1.0)

                            Behavior on scale { NumberAnimation { duration: Shell.Theme.animInstant; easing.type: Easing.OutCubic } }
                            Behavior on color { ColorAnimation { duration: Shell.Theme.animInstant } }
                            Behavior on border.color { ColorAnimation { duration: Shell.Theme.animInstant } }

                            Text {
                                anchors.centerIn: parent
                                text: "󰆴"
                                font.family: Shell.Theme.iconFont
                                font.pixelSize: Shell.Theme.scaled(17)
                                color: clipDelegate.isDeleteFocused ? Shell.Theme.subtext0 : (clipDelegate.isSelected ? Shell.Theme.text : Shell.Theme.subtext0)
                                opacity: delMouse.pressed ? 0.6 : 1.0
                                Behavior on color { ColorAnimation { duration: Shell.Theme.animInstant } }
                                Behavior on opacity { NumberAnimation { duration: Shell.Theme.animInstant } }
                            }

                            MouseArea {
                                id: delMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                preventStealing: true

                                onEntered: {
                                    DynamicIslandService.selectedIndex = index;
                                    DynamicIslandService.deleteFocused = true;
                                }
                                onExited: {
                                    DynamicIslandService.deleteFocused = false;
                                }
                                onClicked: (mouse) => {
                                    mouse.accepted = true;
                                    let item = DynamicIslandService.displayedClips[index] || clipDelegate.modelData;
                                    DynamicIslandService.deleteClipItem(item);
                                }
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: DynamicIslandService.displayedClips.length === 0
                    text: "No clipboard history found"
                    color: Shell.Theme.subtext0
                    font.pixelSize: Shell.Theme.scaled(13)
                }
            }

            // --- EMOJI RESULTS VIEW ---
            Rectangle {
                id: emojiResultsView
                width: parent.width
                height: 320
                implicitHeight: height
                radius: Shell.Theme.scaled(14)
                visible: DynamicIslandService.activeMode === "emoji"
                color: Shell.Theme.glassBackground
                border.color: Shell.Theme.glassBorder
                border.width: 1

                Column {
                    id: emojiColumn
                    width: parent.width - 16
                    height: parent.height - 16
                    x: 8
                    y: 8
                    spacing: 8

                    // Category Selector Pills Bar
                    ScrollView {
                        width: parent.width
                        height: 30
                        contentHeight: 30
                        clip: true
                        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                        ScrollBar.horizontal.policy: ScrollBar.AsNeeded

                        Row {
                            spacing: 6
                            height: 26

                            Repeater {
                                model: DynamicIslandService.emojiCategories
                                delegate: Rectangle {
                                    height: 24
                                    width: catText.implicitWidth + 15
                                    radius: 12
                                    color: DynamicIslandService.selectedCategory === modelData
                                        ? Shell.Theme.accentColor
                                        : (catMouse.containsMouse ? Shell.Theme.cardBackgroundHover : "transparent")
                                    border.width: 0
                                    scale: catMouse.pressed ? 0.94 : 1.0

                                    Behavior on scale { NumberAnimation { duration: Shell.Theme.animInstant; easing.type: Easing.OutCubic } }
                                    Behavior on color { ColorAnimation { duration: Shell.Theme.animInstant } }

                                    Text {
                                        id: catText
                                        anchors.centerIn: parent
                                        text: modelData
                                        font.pixelSize: Shell.Theme.scaled(11)
                                        font.weight: DynamicIslandService.selectedCategory === modelData ? Font.Bold : Font.Normal
                                        color: DynamicIslandService.selectedCategory === modelData
                                            ? ((Shell.Colors && Shell.Colors.on_primary) ? Shell.Colors.on_primary : "#ffffff")
                                            : Shell.Theme.text
                                    }

                                    MouseArea {
                                        id: catMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: DynamicIslandService.setCategory(modelData)
                                    }
                                }
                            }
                        }
                    }

                    // Emoji Grid Container
                    Item {
                        width: parent.width
                        height: parent.height - 38

                        GridView {
                            id: emojiGrid
                            anchors.fill: parent
                            cellWidth: 48
                            cellHeight: 48
                            clip: true
                            model: DynamicIslandService.displayedEmojis
                            currentIndex: DynamicIslandService.selectedIndex

                            visible: DynamicIslandService.displayedEmojis.length > 0

                            delegate: Rectangle {
                                id: emojiDelegate
                                width: 44
                                height: 44
                                radius: 8

                                required property var modelData
                                required property int index

                                readonly property bool isSelected: index === DynamicIslandService.selectedIndex

                                color: isSelected
                                    ? Shell.Theme.accentColor
                                    : (eMouse.containsMouse ? Shell.Theme.cardBackgroundHover : "transparent")
                                scale: eMouse.pressed ? 0.92 : 1.0

                                Behavior on scale { NumberAnimation { duration: Shell.Theme.animInstant; easing.type: Easing.OutCubic } }
                                Behavior on color { ColorAnimation { duration: Shell.Theme.animInstant } }

                                Text {
                                    anchors.centerIn: parent
                                    text: (emojiDelegate.modelData && emojiDelegate.modelData.emoji) ? emojiDelegate.modelData.emoji : ""
                                    font.pixelSize: 26
                                }

                                MouseArea {
                                    id: eMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onEntered: DynamicIslandService.selectedIndex = index
                                    onClicked: DynamicIslandService.copyEmoji(emojiDelegate.modelData.emoji)
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: DynamicIslandService.displayedEmojis.length === 0
                            text: "No emojis found"
                            color: Shell.Theme.subtext0
                            font.pixelSize: Shell.Theme.scaled(13)
                        }
                    }
                }
            }
        }
    }
}
