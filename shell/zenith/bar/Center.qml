import ".."
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Services.Mpris
import "../services"
import "../Settings"
import "./Menu/components"

RowLayout {
    id: root

    property bool mediaPopupOpen: false
    property bool mediaPopupHovered: false
    property real lockedMediaWidth: 0
    property var lockedMediaRect: Qt.rect(0, 0, 0, 0)

    property bool calendarPopupOpen: false
    property bool calendarPopupHovered: false
    property real lockedCalendarWidth: 0
    property var lockedCalendarRect: Qt.rect(0, 0, 0, 0)

    height: Theme.pillHeight
    implicitHeight: Theme.pillHeight
    Layout.preferredHeight: Theme.pillHeight
    Layout.alignment: Qt.AlignVCenter
    spacing: Theme.pillSpacing

    // --- PRODUCTIVITY / POMODORO COUNTDOWN WIDGET ---
    Rectangle {
        visible: (ProductivityService.running || ProductivityService.isBeeping) && !DynamicIslandService.active
        width: timerText.implicitWidth + Theme.scaled(22)
        height: Theme.pillHeight
        radius: height / 2
        color: ProductivityService.isBeeping ? Theme.powerRed : (ProductivityService.running ? Theme.accentColor : Theme.surfaceContainerHigh)
        border.color: Theme.glassBorder
        border.width: 1
        Layout.alignment: Qt.AlignVCenter

        scale: timerMouse.pressed ? 0.95 : 1.0
        Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Theme.animEasing } }
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
        
        SequentialAnimation on opacity {
            running: ProductivityService.isBeeping
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 0.4; duration: 400 }
            NumberAnimation { from: 0.4; to: 1.0; duration: 400 }
        }

        RowLayout {
            anchors.centerIn: parent
            spacing: Theme.scaled(6)

            Text {
                text: ProductivityService.isBeeping ? "󰂚" : "󰔛"
                font.family: Theme.iconFont
                font.pixelSize: Theme.scaled(13)
                color: (ProductivityService.running || ProductivityService.isBeeping) ? Colors.on_primary : Theme.text
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                id: timerText
                text: {
                    if (ProductivityService.isBeeping) return "DONE";
                    let m = Math.floor(ProductivityService.remaining / 60);
                    let s = ProductivityService.remaining % 60;
                    return m + ":" + s.toString().padStart(2, '0');
                }
                font.weight: Font.Black
                font.pixelSize: Theme.scaled(11)
                color: (ProductivityService.running || ProductivityService.isBeeping) ? Colors.on_primary : Theme.text
                Layout.alignment: Qt.AlignVCenter
            }
        }

        MouseArea {
            id: timerMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                if (ProductivityService.isBeeping) {
                    ProductivityService.dismissAlarm();
                } else {
                    CenterState.toggle("Pomodoro");
                }
            }
        }
    }

    // --- MASTERWORK JOINED CENTER PILL / DYNAMIC ISLAND ---
    Rectangle {
        id: centerPill
        radius: height / 2
        color: DynamicIslandService.active
            ? Theme.surfaceContainerHigh
            : Theme.pillColor
        border.color: DynamicIslandService.active ? Theme.accentColor : Theme.glassBorder
        height: Theme.pillHeight
        implicitHeight: Theme.pillHeight
        Layout.preferredHeight: Theme.pillHeight
        Layout.alignment: Qt.AlignVCenter
        width: DynamicIslandService.active
            ? Theme.scaled(480)
            : (centerContent.implicitWidth + Theme.pillPadding + Theme.extraPillPadding)
        implicitWidth: width
        clip: true

        scale: DynamicIslandService.active ? 1.0 : ((mprisMouse.pressed || clockMouse.pressed) ? 0.96 : 1.0)

        Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: Theme.animInstant } }
        Behavior on border.color { ColorAnimation { duration: Theme.animInstant } }

        // --- DEFAULT CENTER CONTENT (Clock/Media/Weather) ---
        RowLayout {
            id: centerContent
            anchors.centerIn: parent
            spacing: Theme.pillGap
            opacity: DynamicIslandService.active ? 0.0 : 1.0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }

            // --- MEDIA / WEATHER SECTION ---
            RowLayout {
                id: mediaWeatherSection
                spacing: Theme.pillGap
                visible: WidgetSettings.enableMedia && !Theme.isSmallScreen

                // Active Playing Media Section
                RowLayout {
                    id: mediaSection
                    spacing: Theme.scaled(6)
                    visible: MediaPlayerService.trackedPlayer && MediaPlayerService.isActuallyPlaying
                    Layout.alignment: Qt.AlignVCenter

                    // Live 4-bar reactive audio equalizer
                    Row {
                        id: eqBars
                        spacing: Theme.scaled(2.5)
                        height: Theme.scaled(14)
                        Layout.alignment: Qt.AlignVCenter

                        Repeater {
                            model: [
                                { minH: 4, maxH: 13, d1: 340, d2: 420 },
                                { minH: 3, maxH: 15, d1: 220, d2: 310 },
                                { minH: 5, maxH: 12, d1: 390, d2: 270 },
                                { minH: 3, maxH: 14, d1: 280, d2: 360 }
                            ]
                            delegate: Rectangle {
                                width: Theme.scaled(2.8)
                                height: Theme.scaled(modelData.minH)
                                radius: width / 2
                                color: mprisMouse.containsMouse 
                                    ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.65) 
                                    : Theme.accentColor
                                anchors.bottom: parent.bottom
                                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                                SequentialAnimation on height {
                                    running: MediaPlayerService.isActuallyPlaying
                                    loops: Animation.Infinite
                                    NumberAnimation { to: Theme.scaled(modelData.maxH); duration: modelData.d1; easing.type: Easing.InOutQuad }
                                    NumberAnimation { to: Theme.scaled(modelData.minH); duration: modelData.d2; easing.type: Easing.InOutQuad }
                                }
                            }
                        }
                    }

                    Text {
                        text: {
                            let p = MediaPlayerService.trackedPlayer;
                            if (!p) return "";
                            let title = MediaPlayerService.formatMediaTitle(String(p.trackTitle || p.identity || "Unknown"), p.identity);
                            let artist = String(p.trackArtist || "");
                            let full = (artist && artist !== "" && artist !== "undefined") ? title + " • " + artist : title;
                            let limit = Theme.isSmallScreen ? 18 : 34;
                            return MediaPlayerService.formatMediaMetadata(full, limit, 5);
                        }
                        color: mprisMouse.containsMouse 
                            ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.65) 
                            : Theme.accentColor
                        font.pixelSize: Theme.fontSize
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.alignment: Qt.AlignVCenter
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    }
                }

                // Weather Section (Displayed when media is idle/paused)
                RowLayout {
                    spacing: Theme.scaled(6)
                    visible: !(MediaPlayerService.trackedPlayer && MediaPlayerService.isActuallyPlaying)

                    Text {
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(15)
                        text: WeatherService.getIcon(WeatherService.weatherCode)
                        color: mprisMouse.containsMouse 
                            ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.65) 
                            : Theme.accentColor
                        Layout.alignment: Qt.AlignVCenter
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    }

                    Text {
                        text: WeatherService.tempC + "°C"
                        color: mprisMouse.containsMouse ? Theme.subtext1 : Theme.text
                        font.pixelSize: Theme.fontSize
                        font.weight: Font.Bold
                        Layout.alignment: Qt.AlignVCenter
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    }

                    Text {
                        text: WeatherService.weatherDesc
                        color: mprisMouse.containsMouse ? Theme.subtext2 : Theme.subtext0
                        font.pixelSize: Theme.scaled(11)
                        visible: text !== "" && !Theme.isSmallScreen
                        Layout.alignment: Qt.AlignVCenter
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    }
                }

                // Elegant Vertical Glass Separator
                Rectangle {
                    id: sectionSeparator
                    width: 1
                    implicitWidth: 1
                    height: Theme.scaled(14)
                    Layout.alignment: Qt.AlignVCenter
                    Layout.leftMargin: Theme.scaled(2)
                    Layout.rightMargin: Theme.scaled(2)
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(255, 255, 255, 0.0) }
                        GradientStop { position: 0.5; color: Qt.rgba(255, 255, 255, 0.35) }
                        GradientStop { position: 1.0; color: Qt.rgba(255, 255, 255, 0.0) }
                    }
                }
            }

            // --- CLOCK SECTION ---
            RowLayout {
                id: clockSection
                spacing: Theme.scaled(6)
                Layout.alignment: Qt.AlignVCenter

                Text {
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.scaled(13)
                    text: "󰥔"
                    color: clockMouse.containsMouse ? Theme.subtext2 : Theme.subtext0
                    Layout.alignment: Qt.AlignVCenter
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }

                Text {
                    visible: ClockSettings.showDate
                    color: clockMouse.containsMouse ? Theme.subtext0 : Theme.fontColor
                    font.pixelSize: Theme.fontSize
                    font.weight: Font.Normal
                    Layout.alignment: Qt.AlignVCenter
                    text: Qt.formatDateTime(systemClock.date, ClockSettings.dateFormat)
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }

                // Elegant Vertical Glass Separator
                Rectangle {
                    visible: ClockSettings.showDate && ClockSettings.showClock
                    width: 1
                    implicitWidth: 1
                    height: Theme.scaled(14)
                    Layout.alignment: Qt.AlignVCenter
                    Layout.leftMargin: Theme.scaled(2)
                    Layout.rightMargin: Theme.scaled(2)
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(255, 255, 255, 0.0) }
                        GradientStop { position: 0.5; color: Qt.rgba(255, 255, 255, 0.35) }
                        GradientStop { position: 1.0; color: Qt.rgba(255, 255, 255, 0.0) }
                    }
                }

                Text {
                    visible: ClockSettings.showClock
                    color: clockMouse.containsMouse ? Theme.subtext0 : Theme.fontColor
                    font.pixelSize: Theme.fontSize
                    font.weight: Font.Normal
                    Layout.alignment: Qt.AlignVCenter
                    text: {
                        let timeFmt = ClockSettings.use24Hour ? ClockSettings.timeFormat24h : ClockSettings.timeFormat12h;
                        return Qt.formatDateTime(systemClock.date, timeFmt);
                    }
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }
            }
        }

        // --- DYNAMIC ISLAND SEARCH BAR CONTENT ---
        RowLayout {
            id: dynamicIslandSearchContent
            anchors.fill: parent
            anchors.leftMargin: Theme.scaled(10)
            anchors.rightMargin: Theme.scaled(10)
            spacing: Theme.scaled(8)
            opacity: DynamicIslandService.active ? 1.0 : 0.0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutQuad } }

            // Mode Selector Buttons
            RowLayout {
                spacing: Theme.scaled(4)
                Layout.alignment: Qt.AlignVCenter

                // Launcher / Apps Tab
                Rectangle {
                    width: Theme.scaled(28)
                    height: Theme.scaled(22)
                    radius: Theme.scaled(6)
                    color: DynamicIslandService.activeMode === "launcher" ? Theme.accentColor : (appTabMouse.containsMouse ? Theme.surfaceContainerHigh : "transparent")
                    
                    Text {
                        anchors.centerIn: parent
                        text: "󰀻"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(13)
                        color: DynamicIslandService.activeMode === "launcher" ? Colors.on_primary : Theme.text
                    }

                    MouseArea {
                        id: appTabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            DynamicIslandService.setMode("launcher");
                            searchInput.forceActiveFocus();
                        }
                    }
                }

                // Clipboard Tab
                Rectangle {
                    width: Theme.scaled(28)
                    height: Theme.scaled(22)
                    radius: Theme.scaled(6)
                    color: DynamicIslandService.activeMode === "clipboard" ? Theme.accentColor : (clipTabMouse.containsMouse ? Theme.surfaceContainerHigh : "transparent")
                    
                    Text {
                        anchors.centerIn: parent
                        text: "󰅍"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(13)
                        color: DynamicIslandService.activeMode === "clipboard" ? Colors.on_primary : Theme.text
                    }

                    MouseArea {
                        id: clipTabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            DynamicIslandService.setMode("clipboard");
                            searchInput.forceActiveFocus();
                        }
                    }
                }

                // Emoji Tab
                Rectangle {
                    width: Theme.scaled(28)
                    height: Theme.scaled(22)
                    radius: Theme.scaled(6)
                    color: DynamicIslandService.activeMode === "emoji" ? Theme.accentColor : (emojiTabMouse.containsMouse ? Theme.surfaceContainerHigh : "transparent")
                    
                    Text {
                        anchors.centerIn: parent
                        text: "󰞅"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(13)
                        color: DynamicIslandService.activeMode === "emoji" ? Colors.on_primary : Theme.text
                    }

                    MouseArea {
                        id: emojiTabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            DynamicIslandService.setMode("emoji");
                            searchInput.forceActiveFocus();
                        }
                    }
                }
            }

            // Vertical Glass Separator
            Rectangle {
                width: 1
                height: Theme.scaled(14)
                color: Theme.glassBorder
                Layout.alignment: Qt.AlignVCenter
            }

            // Search Input Container
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Timer {
                    id: searchDebounce
                    interval: 15
                    repeat: false
                    onTriggered: {
                        DynamicIslandService.query = searchInput.text;
                        DynamicIslandService.rebuildFiltered();
                    }
                }

                TextInput {
                    id: searchInput
                    anchors.fill: parent
                    verticalAlignment: TextInput.AlignVCenter
                    font.pixelSize: Theme.fontSize
                    color: Theme.text
                    selectByMouse: true
                    focus: DynamicIslandService.active

                    Connections {
                        target: DynamicIslandService
                        function onActiveChanged() {
                            if (DynamicIslandService.active) {
                                searchDebounce.stop();
                                searchInput.text = DynamicIslandService.query;
                                Qt.callLater(() => searchInput.forceActiveFocus());
                            } else {
                                searchDebounce.stop();
                                searchInput.text = "";
                            }
                        }
                        function onActiveModeChanged() {
                            if (DynamicIslandService.active) {
                                searchDebounce.stop();
                                searchInput.text = "";
                                Qt.callLater(() => searchInput.forceActiveFocus());
                            }
                        }
                    }

                    onTextChanged: {
                        if (!DynamicIslandService.active) return;
                        if (text === "") {
                            searchDebounce.stop();
                            DynamicIslandService.query = "";
                            DynamicIslandService.rebuildFiltered();
                        } else {
                            searchDebounce.restart();
                        }
                    }

                    Keys.onEscapePressed: DynamicIslandService.close()

                    Keys.onTabPressed: (event) => {
                        DynamicIslandService.cycleMode();
                        event.accepted = true;
                    }

                    Keys.onLeftPressed: (event) => {
                        if (DynamicIslandService.activeMode === "emoji") {
                            let maxIdx = DynamicIslandService.displayedEmojis.length - 1;
                            if (maxIdx >= 0) {
                                DynamicIslandService.selectedIndex = Math.max(0, DynamicIslandService.selectedIndex - 1);
                                event.accepted = true;
                            }
                        } else if (DynamicIslandService.activeMode === "clipboard") {
                            if (DynamicIslandService.deleteFocused) {
                                DynamicIslandService.deleteFocused = false;
                                event.accepted = true;
                            }
                        }
                    }

                    Keys.onRightPressed: (event) => {
                        if (DynamicIslandService.activeMode === "emoji") {
                            let maxIdx = DynamicIslandService.displayedEmojis.length - 1;
                            if (maxIdx >= 0) {
                                DynamicIslandService.selectedIndex = Math.min(maxIdx, DynamicIslandService.selectedIndex + 1);
                                event.accepted = true;
                            }
                        } else if (DynamicIslandService.activeMode === "clipboard") {
                            if (!DynamicIslandService.deleteFocused && DynamicIslandService.displayedClips.length > 0) {
                                DynamicIslandService.deleteFocused = true;
                                event.accepted = true;
                            }
                        }
                    }

                    Keys.onDownPressed: (event) => {
                        DynamicIslandService.deleteFocused = false;
                        let maxIdx = 0;
                        if (DynamicIslandService.activeMode === "launcher") {
                            maxIdx = DynamicIslandService.displayedApps.length - 1;
                            if (maxIdx >= 0) DynamicIslandService.selectedIndex = Math.min(maxIdx, DynamicIslandService.selectedIndex + 1);
                        } else if (DynamicIslandService.activeMode === "clipboard") {
                            maxIdx = DynamicIslandService.displayedClips.length - 1;
                            if (maxIdx >= 0) DynamicIslandService.selectedIndex = Math.min(maxIdx, DynamicIslandService.selectedIndex + 1);
                        } else if (DynamicIslandService.activeMode === "emoji") {
                            maxIdx = DynamicIslandService.displayedEmojis.length - 1;
                            if (maxIdx >= 0) DynamicIslandService.selectedIndex = Math.min(maxIdx, DynamicIslandService.selectedIndex + 12);
                        }
                        if (event) event.accepted = true;
                    }

                    Keys.onUpPressed: (event) => {
                        DynamicIslandService.deleteFocused = false;
                        let maxIdx = 0;
                        if (DynamicIslandService.activeMode === "launcher") {
                            maxIdx = DynamicIslandService.displayedApps.length - 1;
                            if (maxIdx >= 0) DynamicIslandService.selectedIndex = Math.max(0, DynamicIslandService.selectedIndex - 1);
                        } else if (DynamicIslandService.activeMode === "clipboard") {
                            maxIdx = DynamicIslandService.displayedClips.length - 1;
                            if (maxIdx >= 0) DynamicIslandService.selectedIndex = Math.max(0, DynamicIslandService.selectedIndex - 1);
                        } else if (DynamicIslandService.activeMode === "emoji") {
                            maxIdx = DynamicIslandService.displayedEmojis.length - 1;
                            if (maxIdx >= 0) DynamicIslandService.selectedIndex = Math.max(0, DynamicIslandService.selectedIndex - 12);
                        }
                        if (event) event.accepted = true;
                    }

                    Keys.onDeletePressed: (event) => {
                        if (DynamicIslandService.activeMode === "clipboard") {
                            let idx = DynamicIslandService.selectedIndex;
                            if (idx >= 0 && idx < DynamicIslandService.displayedClips.length) {
                                DynamicIslandService.deleteClipItem(DynamicIslandService.displayedClips[idx]);
                                DynamicIslandService.deleteFocused = false;
                                if (event) event.accepted = true;
                            }
                        }
                    }

                    Keys.onReturnPressed: (event) => handleEnter(event)
                    Keys.onEnterPressed: (event) => handleEnter(event)

                    function handleEnter(event) {
                        if (searchDebounce.running) {
                            searchDebounce.stop();
                            DynamicIslandService.query = searchInput.text;
                            DynamicIslandService.rebuildFiltered();
                        }

                        let isShift = event && (event.modifiers & Qt.ShiftModifier);
                        if (isShift && DynamicIslandService.activeMode === "launcher") {
                            DynamicIslandService.runInTerminal(searchInput.text || DynamicIslandService.query);
                            DynamicIslandService.close();
                            if (event) event.accepted = true;
                            return;
                        }

                        let idx = DynamicIslandService.selectedIndex;
                        if (DynamicIslandService.activeMode === "launcher") {
                            if (idx >= 0 && idx < DynamicIslandService.displayedApps.length) {
                                DynamicIslandService.launchApp(DynamicIslandService.displayedApps[idx]);
                            } else {
                                let cmd = (searchInput.text || DynamicIslandService.query).trim();
                                if (cmd !== "") {
                                    DynamicIslandService.runInTerminal(cmd);
                                }
                                DynamicIslandService.close();
                            }
                        } else if (DynamicIslandService.activeMode === "clipboard") {
                            if (idx >= 0 && idx < DynamicIslandService.displayedClips.length) {
                                if (DynamicIslandService.deleteFocused) {
                                    DynamicIslandService.deleteClipItem(DynamicIslandService.displayedClips[idx]);
                                    DynamicIslandService.deleteFocused = false;
                                } else {
                                    DynamicIslandService.copyClipItem(DynamicIslandService.displayedClips[idx]);
                                }
                            }
                        } else if (DynamicIslandService.activeMode === "emoji") {
                            if (idx >= 0 && idx < DynamicIslandService.displayedEmojis.length) {
                                DynamicIslandService.copyEmoji(DynamicIslandService.displayedEmojis[idx].emoji);
                            }
                        }
                        if (event) event.accepted = true;
                    }

                    Text {
                        text: {
                            if (DynamicIslandService.activeMode === "launcher") return "Search apps or calc (e.g. 6 + 6)...";
                            if (DynamicIslandService.activeMode === "clipboard") return "Search clipboard history...";
                            if (DynamicIslandService.activeMode === "emoji") return "Search emojis...";
                            return "Search...";
                        }
                        color: Theme.subtext0
                        font.pixelSize: Theme.fontSize
                        visible: searchInput.text.length === 0
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            // Close Button
            Text {
                text: "󰅖"
                font.family: Theme.iconFont
                font.pixelSize: Theme.scaled(13)
                color: closeMouse.containsMouse ? Theme.powerRed : Theme.subtext0
                Layout.alignment: Qt.AlignVCenter

                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    anchors.margins: -Theme.scaled(4)
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: DynamicIslandService.close()
                }
            }
        }

        SystemClock {
            id: systemClock
            precision: ClockSettings.precision
        }

        // Dual-zone mouse areas: MPRIS/Media on the left, Date/Clock on the right
        Item {
            id: pillMouseAreaContainer
            anchors.fill: parent
            enabled: !DynamicIslandService.active

            // Left side: MPRIS / Media hover area
            MouseArea {
                id: mprisMouse
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: mediaWeatherSection.visible 
                    ? Math.max(0, (centerContent.x + mediaWeatherSection.x + mediaWeatherSection.width))
                    : 0
                visible: width > 0
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton

                onContainsMouseChanged: {
                    if (containsMouse) {
                        calendarShowTimer.stop();
                        if (root.calendarPopupOpen) {
                            calendarHideTimer.restart();
                        }
                        if (MediaPlayerService.trackedPlayer && !CenterState.qsVisible && !DynamicIslandService.active) {
                            mediaHideTimer.stop();
                            mediaShowTimer.restart();
                        }
                    } else {
                        mediaShowTimer.stop();
                        mediaHideTimer.restart();
                    }
                }

                onClicked: (mouse) => {
                    if (mouse.button === Qt.LeftButton) {
                        root.mediaPopupOpen = false;
                        root.calendarPopupOpen = false;
                        CenterState.toggle("Default");
                    } else if (mouse.button === Qt.RightButton && MediaPlayerService.trackedPlayer) {
                        let p = MediaPlayerService.trackedPlayer;
                        if (p.playPause) p.playPause();
                        else if (MediaPlayerService.isActuallyPlaying) p.pause();
                        else p.play();
                    }
                }

                onWheel: (wheel) => {
                    let p = MediaPlayerService.trackedPlayer;
                    if (p && MediaPlayerService.isActuallyPlaying) {
                        if (wheel.angleDelta.y < 0) {
                            if (p.next) p.next();
                        } else if (wheel.angleDelta.y > 0) {
                            if (p.previous) p.previous();
                        }
                    }
                }
            }

            // Right side: Clock / Date hover area
            MouseArea {
                id: clockMouse
                anchors.left: mprisMouse.visible ? mprisMouse.right : parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton

                onContainsMouseChanged: {
                    if (containsMouse) {
                        mediaShowTimer.stop();
                        if (root.mediaPopupOpen) {
                            mediaHideTimer.restart();
                        }
                        if (!CenterState.qsVisible && !DynamicIslandService.active) {
                            calendarHideTimer.stop();
                            calendarShowTimer.restart();
                        }
                    } else {
                        calendarShowTimer.stop();
                        calendarHideTimer.restart();
                    }
                }

                onClicked: (mouse) => {
                    root.mediaPopupOpen = false;
                    root.calendarPopupOpen = false;
                    CenterState.toggle("Default");
                }
            }
        }

        // --- MEDIA TOOLTIP TIMERS ---
        Timer {
            id: mediaShowTimer
            interval: 180
            onTriggered: {
                if (mprisMouse.containsMouse && MediaPlayerService.trackedPlayer && !CenterState.qsVisible && !DynamicIslandService.active) {
                    calendarShowTimer.stop();
                    root.calendarPopupOpen = false;
                    mediaHideTimer.stop();
                    let pillRect = centerPill.mapToItem(null, 0, 0, centerPill.width, centerPill.height);
                    root.lockedMediaRect = Qt.rect(pillRect.x, pillRect.y, pillRect.width, pillRect.height);
                    root.lockedMediaWidth = Math.max(centerPill.width, Theme.scaled(440));
                    root.mediaPopupOpen = true;
                }
            }
        }

        Timer {
            id: mediaHideTimer
            interval: 350
            onTriggered: {
                if (!mprisMouse.containsMouse && !root.mediaPopupHovered) {
                    root.mediaPopupOpen = false;
                    root.lockedMediaWidth = 0;
                    root.lockedMediaRect = Qt.rect(0, 0, 0, 0);
                }
            }
        }

        // --- CALENDAR TOOLTIP TIMERS ---
        Timer {
            id: calendarShowTimer
            interval: 180
            onTriggered: {
                if (clockMouse.containsMouse && !CenterState.qsVisible && !DynamicIslandService.active) {
                    mediaShowTimer.stop();
                    root.mediaPopupOpen = false;
                    calendarHideTimer.stop();
                    let pillRect = centerPill.mapToItem(null, 0, 0, centerPill.width, centerPill.height);
                    root.lockedCalendarRect = Qt.rect(pillRect.x, pillRect.y, pillRect.width, pillRect.height);
                    root.lockedCalendarWidth = Theme.scaled(340);
                    root.calendarPopupOpen = true;
                }
            }
        }

        Timer {
            id: calendarHideTimer
            interval: 350
            onTriggered: {
                if (!clockMouse.containsMouse && !root.calendarPopupHovered) {
                    root.calendarPopupOpen = false;
                    root.lockedCalendarWidth = 0;
                    root.lockedCalendarRect = Qt.rect(0, 0, 0, 0);
                }
            }
        }

        Connections {
            target: CenterState
            function onQsVisibleChanged() {
                if (CenterState.qsVisible) {
                    root.mediaPopupOpen = false;
                    root.calendarPopupOpen = false;
                }
            }
        }

        Connections {
            target: DynamicIslandService
            function onActiveChanged() {
                if (DynamicIslandService.active) {
                    root.mediaPopupOpen = false;
                    root.calendarPopupOpen = false;
                }
            }
        }

        // --- MPRIS MUSIC PLAYER TOOLTIP POPUP ---
        Loader {
            id: mediaTooltipLoader
            active: root.mediaPopupOpen && MediaPlayerService.trackedPlayer && !CenterState.qsVisible && !DynamicIslandService.active

            sourceComponent: PopupWindow {
                id: mediaTooltipPopup

                visible: true
                color: "transparent"

                function updateHoverState() {
                    let isHov = (hoverArea && hoverArea.containsMouse) || (mprisComp && mprisComp.isPlayerHovered);
                    root.mediaPopupHovered = isHov;
                    if (isHov) {
                        mediaHideTimer.stop();
                    } else {
                        mediaHideTimer.restart();
                    }
                }

                Component.onCompleted: {
                    mediaTooltipPopup.updateHoverState();
                    openAnim.restart();
                }
                Component.onDestruction: {
                    root.mediaPopupHovered = false;
                }

                anchor.window: centerPill.QsWindow.window
                anchor.rect: root.lockedMediaRect.width > 0 
                    ? root.lockedMediaRect 
                    : centerPill.mapToItem(null, 0, 0, centerPill.width, centerPill.height)
                anchor.edges: Edges.Bottom
                anchor.gravity: Edges.Bottom

                implicitWidth: root.lockedMediaWidth > 0 ? root.lockedMediaWidth : Math.max(centerPill.width, Theme.scaled(480))
                implicitHeight: Theme.scaled(132)

                Shortcut {
                    sequences: ["Escape"]
                    onActivated: root.mediaPopupOpen = false
                }

                ParallelAnimation {
                    id: openAnim
                    NumberAnimation {
                        target: popupSurface
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: Theme.animNormal
                        easing.type: Easing.OutCubic
                    }
                    NumberAnimation {
                        target: popTranslate
                        property: "y"
                        from: -8
                        to: 0
                        duration: Theme.animNormal
                        easing.type: Easing.OutCubic
                    }
                }

                Item {
                    anchors.fill: parent

                    Rectangle {
                        id: popupSurface
                        y: Theme.scaled(6)
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: root.lockedMediaWidth > 0 ? root.lockedMediaWidth : Math.max(centerPill.width, Theme.scaled(480))
                        height: Theme.scaled(120)
                        color: Theme.glassBackground
                        border.color: root.mediaPopupHovered 
                            ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.50) 
                            : Theme.glassBorder
                        border.width: 1
                        radius: Theme.cardRadius
                        clip: true

                        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                        transform: Translate { id: popTranslate; y: -8 }

                        MprisPlayer {
                            id: mprisComp
                            anchors.centerIn: parent
                            width: parent.width - Theme.scaled(12)
                            height: parent.height - Theme.scaled(12)
                            active: true
                            color: "transparent"
                            border.width: 0

                            onIsPlayerHoveredChanged: mediaTooltipPopup.updateHoverState()
                        }
                    }

                    // Full-window transparent hover detection overlay
                    MouseArea {
                        id: hoverArea
                        anchors.fill: parent
                        z: 99
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                        onContainsMouseChanged: mediaTooltipPopup.updateHoverState()
                    }
                }
            }
        }

        // --- CALENDAR TOOLTIP POPUP ---
        Loader {
            id: calendarTooltipLoader
            active: root.calendarPopupOpen && !CenterState.qsVisible && !DynamicIslandService.active

            sourceComponent: PopupWindow {
                id: calendarTooltipPopup

                visible: true
                color: "transparent"

                function updateHoverState() {
                    let isHov = hoverArea && hoverArea.containsMouse;
                    root.calendarPopupHovered = isHov;
                    if (isHov) {
                        calendarHideTimer.stop();
                    } else {
                        calendarHideTimer.restart();
                    }
                }

                Component.onCompleted: {
                    calendarTooltipPopup.updateHoverState();
                    calOpenAnim.restart();
                }
                Component.onDestruction: {
                    root.calendarPopupHovered = false;
                }

                anchor.window: centerPill.QsWindow.window
                anchor.rect: root.lockedCalendarRect.width > 0 
                    ? root.lockedCalendarRect 
                    : centerPill.mapToItem(null, 0, 0, centerPill.width, centerPill.height)
                anchor.edges: Edges.Bottom
                anchor.gravity: Edges.Bottom

                implicitWidth: root.lockedCalendarWidth > 0 ? root.lockedCalendarWidth : Theme.scaled(340)
                implicitHeight: Theme.scaled(352)

                Shortcut {
                    sequences: ["Escape"]
                    onActivated: root.calendarPopupOpen = false
                }

                ParallelAnimation {
                    id: calOpenAnim
                    NumberAnimation {
                        target: calPopupSurface
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: Theme.animNormal
                        easing.type: Easing.OutCubic
                    }
                    NumberAnimation {
                        target: calPopTranslate
                        property: "y"
                        from: -8
                        to: 0
                        duration: Theme.animNormal
                        easing.type: Easing.OutCubic
                    }
                }

                Item {
                    anchors.fill: parent

                    Rectangle {
                        id: calPopupSurface
                        y: Theme.scaled(6)
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: root.lockedCalendarWidth > 0 ? root.lockedCalendarWidth : Theme.scaled(340)
                        height: Theme.scaled(340)
                        color: Theme.glassBackground
                        border.color: root.calendarPopupHovered 
                            ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.50) 
                            : Theme.glassBorder
                        border.width: 1
                        radius: Theme.cardRadius
                        clip: true

                        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                        transform: Translate { id: calPopTranslate; y: -8 }

                        CalendarWidget {
                            id: calComp
                            anchors.fill: parent
                            color: "transparent"
                            border.width: 0
                        }
                    }

                    // Full-window transparent hover detection overlay
                    MouseArea {
                        id: hoverArea
                        anchors.fill: parent
                        z: 99
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                        onContainsMouseChanged: calendarTooltipPopup.updateHoverState()
                    }
                }
            }
        }
    }
}
