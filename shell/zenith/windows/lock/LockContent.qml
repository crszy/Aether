// The inside of the lock card: weather + media on the left, the clock /
// profile / password column in the middle, system resources on the right.
import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Quickshell
import Quickshell.Io
import "../.."
import "../../services"
import "../../Settings"

RowLayout {
    id: root

    required property var surface
    readonly property var player: MediaPlayerService.trackedPlayer
    readonly property real centerScale: surface.centerScale
    readonly property int centerWidth: surface.centerWidth
    readonly property int panelRadius: Theme.scaled(20)
    readonly property color panelColor: Qt.alpha(Colors.surface_container, 0.9)

    spacing: Theme.scaled(28)

    // ---- Left: weather, media ------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: Theme.scaled(12)

        Rectangle {
            id: weatherCard
            Layout.fillWidth: true
            implicitHeight: weatherCol.implicitHeight + Theme.scaled(28)
            radius: root.panelRadius
            color: root.panelColor
            visible: WidgetSettings.enableWeather

            readonly property var cur: WeatherService.weatherData?.current_condition?.[0]
            readonly property string area: WeatherService.weatherData?.nearest_area?.[0]?.areaName?.[0]?.value || WeatherService.areaName || "Weather"
            readonly property string temp: (cur?.temp_C || WeatherService.tempC || "0") + "°C"
            readonly property string feelsLike: cur?.FeelsLikeC ? (cur.FeelsLikeC + "°C") : temp
            readonly property string humidity: cur?.humidity ? (cur.humidity + "%") : ""
            readonly property string wind: cur?.windspeedKmph ? (cur.windspeedKmph + " km/h") : ""
            readonly property string desc: cur?.weatherDesc?.[0]?.value || WeatherService.weatherDesc || "Clear"

            ColumnLayout {
                id: weatherCol
                anchors.fill: parent
                anchors.margins: Theme.scaled(14)
                spacing: Theme.scaled(10)

                // Top row: Location & Weather Tag
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)

                    Text {
                        text: "📍 " + weatherCard.area
                        color: Theme.accentColor
                        font.pixelSize: Theme.scaled(11)
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Text {
                        text: "WEATHER"
                        color: Theme.subtext0
                        font.pixelSize: Theme.scaled(10)
                        font.weight: Font.Black
                        font.letterSpacing: 1
                    }
                }

                // Middle row: Large Icon + Temp + Condition
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(14)

                    Text {
                        text: WeatherService.getIcon(WeatherService.weatherCode)
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(34)
                        color: Theme.accentColor
                        Layout.alignment: Qt.AlignVCenter
                    }

                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Text {
                            text: weatherCard.temp
                            color: Theme.text
                            font.pixelSize: Theme.scaled(22)
                            font.weight: Font.Bold
                        }
                        Text {
                            text: weatherCard.desc
                            color: Theme.subtext0
                            font.pixelSize: Theme.scaled(12)
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }

                // Bottom row: Metrics Chips (Feels Like, Humidity, Wind)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    visible: weatherCard.cur !== undefined

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Theme.scaled(22)
                        radius: height / 2
                        color: Qt.alpha(Colors.surface_container_highest, 0.6)

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: Theme.scaled(4)
                            Text { text: "󰔏"; font.family: Theme.iconFont; font.pixelSize: Theme.scaled(11); color: Theme.accentColor }
                            Text { text: "Feels " + weatherCard.feelsLike; font.pixelSize: Theme.scaled(10); color: Theme.subtext1; font.weight: Font.Medium }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Theme.scaled(22)
                        radius: height / 2
                        color: Qt.alpha(Colors.surface_container_highest, 0.6)
                        visible: weatherCard.humidity !== ""

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: Theme.scaled(4)
                            Text { text: "󰖎"; font.family: Theme.iconFont; font.pixelSize: Theme.scaled(11); color: Colors.secondary }
                            Text { text: weatherCard.humidity; font.pixelSize: Theme.scaled(10); color: Theme.subtext1; font.weight: Font.Medium }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Theme.scaled(22)
                        radius: height / 2
                        color: Qt.alpha(Colors.surface_container_highest, 0.6)
                        visible: weatherCard.wind !== ""

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: Theme.scaled(4)
                            Text { text: "󰖐"; font.family: Theme.iconFont; font.pixelSize: Theme.scaled(11); color: Theme.subtext0 }
                            Text { text: weatherCard.wind; font.pixelSize: Theme.scaled(10); color: Theme.subtext1; font.weight: Font.Medium }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: root.panelRadius
            color: root.panelColor
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(16)
                spacing: Theme.scaled(12)

                Item {
                    id: artBox
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    readonly property int artRadius: Theme.scaled(12)

                    // Rounded background
                    Rectangle {
                        anchors.fill: parent
                        radius: artBox.artRadius
                        color: Qt.alpha(Colors.surface_container_high, 0.8)
                        border.color: Theme.glassBorder
                        border.width: 1
                    }

                    // Rounded mask for artwork
                    Rectangle {
                        id: artMask
                        anchors.fill: parent
                        radius: artBox.artRadius
                        color: "black"
                        layer.enabled: true
                        visible: false
                    }

                    Image {
                        id: art
                        anchors.fill: parent
                        source: root.player ? String(root.player.trackArtUrl || "") : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        visible: status === Image.Ready

                        layer.enabled: true
                        layer.effect: MultiEffect {
                            maskEnabled: true
                            maskSource: artMask
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: art.status !== Image.Ready
                        text: "󰎆"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(64)
                        color: Colors.on_surface_variant
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.player ? MediaPlayerService.formatMediaTitle(String(root.player.trackTitle || "Media"), root.player.identity) : "Nothing playing"
                    color: Theme.text
                    font.pixelSize: Theme.scaled(14)
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: root.player ? String(root.player.trackArtist || "") : ""
                    visible: text !== ""
                    color: Theme.subtext0
                    font.pixelSize: Theme.scaled(12)
                    elide: Text.ElideRight
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Theme.scaled(4)
                    spacing: Theme.scaled(14)
                    visible: root.player !== null

                    Rectangle {
                        implicitWidth: Theme.scaled(32); implicitHeight: Theme.scaled(32)
                        radius: width / 2
                        color: prevM.containsMouse ? Qt.alpha(Colors.primary, 0.2) : Qt.alpha(Colors.surface_container_highest, 0.6)
                        Text {
                            anchors.centerIn: parent
                            text: "󰒮"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(15)
                            color: prevM.containsMouse ? Colors.primary : Theme.text
                        }
                        MouseArea {
                            id: prevM
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: if (root.player && root.player.canGoPrevious) root.player.previous()
                        }
                    }

                    Rectangle {
                        implicitWidth: Theme.scaled(36); implicitHeight: Theme.scaled(36)
                        radius: width / 2
                        color: Colors.primary
                        scale: playM.pressed ? 0.92 : 1.0
                        Behavior on scale { NumberAnimation { duration: Theme.animInstant } }
                        Text {
                            anchors.centerIn: parent
                            text: MediaPlayerService.isActuallyPlaying ? "󰏤" : "󰐊"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(16)
                            color: Colors.on_primary
                        }
                        MouseArea {
                            id: playM
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (!root.player) return;
                                if (MediaPlayerService.isActuallyPlaying) root.player.pause();
                                else root.player.play();
                            }
                        }
                    }

                    Rectangle {
                        implicitWidth: Theme.scaled(32); implicitHeight: Theme.scaled(32)
                        radius: width / 2
                        color: nextM.containsMouse ? Qt.alpha(Colors.primary, 0.2) : Qt.alpha(Colors.surface_container_highest, 0.6)
                        Text {
                            anchors.centerIn: parent
                            text: "󰒭"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(15)
                            color: nextM.containsMouse ? Colors.primary : Theme.text
                        }
                        MouseArea {
                            id: nextM
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: if (root.player && root.player.canGoNext) root.player.next()
                        }
                    }
                }
            }
        }
    }

    // ---- Center ----------------------------------------------------------
    ColumnLayout {
        Layout.preferredWidth: root.centerWidth
        Layout.fillWidth: false
        Layout.fillHeight: true
        spacing: Theme.scaled(16)

        // Clock (12-hour format with AM/PM pill below minutes)
        RowLayout {
            id: clockRow
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.scaled(8) * root.centerScale
            spacing: Theme.scaled(8)

            readonly property int digitSize: Math.round((ClockSettings.use24Hour ? 148 : 132) * root.centerScale)
            readonly property int rawHours: clock.date.getHours()
            readonly property int hours12: (rawHours % 12 === 0) ? 12 : (rawHours % 12)
            readonly property string hoursStr: (hours12 < 10 ? "0" : "") + hours12
            readonly property string ampmStr: rawHours >= 12 ? "PM" : "AM"

            Text {
                Layout.alignment: Qt.AlignTop
                text: ClockSettings.use24Hour ? Qt.formatDateTime(clock.date, "HH") : clockRow.hoursStr
                color: Colors.primary
                font.pixelSize: clockRow.digitSize
                font.weight: Font.Black
                font.letterSpacing: -4
            }

            Text {
                text: ":"
                color: Qt.alpha(Colors.primary, 0.45)
                font.pixelSize: Math.round(clockRow.digitSize * 0.75)
                font.weight: Font.Light
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Theme.scaled(6) * root.centerScale
            }

            ColumnLayout {
                spacing: 0

                Text {
                    id: minutesText
                    text: Qt.formatDateTime(clock.date, "mm")
                    color: Colors.secondary
                    font.pixelSize: clockRow.digitSize
                    font.weight: Font.Black
                    font.letterSpacing: -4
                }

                Rectangle {
                    visible: !ClockSettings.use24Hour
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: -Theme.scaled(14)
                    implicitWidth: minutesText.implicitWidth - Theme.scaled(8)
                    implicitHeight: ampm.implicitHeight + Theme.scaled(10)
                    radius: Theme.scaled(12)
                    color: Qt.alpha(Colors.surface_container_high, 0.9)

                    Text {
                        id: ampm
                        anchors.centerIn: parent
                        text: clockRow.ampmStr
                        color: Theme.text
                        font.pixelSize: Theme.scaled(18)
                        font.weight: Font.Bold
                        font.letterSpacing: 2
                    }
                }
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: Qt.formatDateTime(clock.date, "dddd • d MMM").toUpperCase()
            color: Theme.text
            font.pixelSize: Theme.scaled(15)
            font.weight: Font.DemiBold
            font.letterSpacing: 2
        }

        // Profile picture
        Item {
            id: pfpBox
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.scaled(14) * root.centerScale
            Layout.bottomMargin: Theme.scaled(8) * root.centerScale
            readonly property int size: Math.round(root.centerWidth * 0.44)
            Layout.preferredWidth: size
            Layout.preferredHeight: size
            implicitWidth: size
            implicitHeight: size
            width: size
            height: size
            property bool hasFace: false

            Process {
                running: true
                command: ["test", "-f", PathSettings.home + "/.face"]
                onExited: (code) => pfpBox.hasFace = (code === 0)
            }

            // Outer ring border
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Qt.alpha(Colors.surface_container_highest, 0.9)
                border.color: Qt.alpha(Colors.primary, 0.6)
                border.width: 2.5
            }

            // Fallback glyph
            Text {
                anchors.centerIn: parent
                visible: pfp.status !== Image.Ready
                text: "󰀄"
                font.family: Theme.iconFont
                font.pixelSize: parent.size / 2
                color: Colors.on_surface_variant
            }

            // Circular mask
            Rectangle {
                id: pfpMask
                anchors.fill: parent
                anchors.margins: 3
                radius: width / 2
                color: "black"
                layer.enabled: true
                visible: false
            }

            // Avatar source image
            Image {
                id: pfp
                anchors.fill: pfpMask
                source: {
                    const custom = IdleSettings.profilePicture;
                    if (custom !== "") return custom.startsWith("file:///") ? custom : "file:///" + custom;
                    if (pfpBox.hasFace) return "file:///" + PathSettings.home + "/.face";
                    return "file:///" + PathSettings.shellDir + "/pp.png";
                }
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: status === Image.Ready

                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: pfpMask
                }

                onStatusChanged: {
                    if (status === Image.Error && source.toString().endsWith("/pp.png")) {
                        source = "file:///" + PathSettings.shellDir + "/pp";
                    }
                }
            }
        }

        // User identification
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Theme.scaled(10) * root.centerScale
            spacing: Theme.scaled(2)

            Text {
                id: userNameDisplay
                Layout.alignment: Qt.AlignHCenter
                text: Variables.user
                color: Theme.text
                font.pixelSize: Theme.scaled(20)
                font.weight: Font.Bold
                font.capitalization: Font.Capitalize
                font.letterSpacing: 0.5
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "@" + Variables.hostname
                color: Theme.subtext0
                font.pixelSize: Theme.scaled(12)
                font.weight: Font.Medium
                opacity: 0.75
            }
        }

        // Password pill
        Rectangle {
            id: field
            property bool revealPassword: false
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: Math.round(root.centerWidth * 0.85)
            implicitHeight: Theme.scaled(48)
            radius: height / 2
            clip: true
            color: Qt.alpha(Colors.surface_container_high, 0.95)
            border.width: 2
            border.color: {
                if (LockService.state === LockService.State.Failed || LockService.state === LockService.State.MaxTries) return Colors.error;
                if (LockService.authenticating) return Colors.primary;
                return "transparent";
            }
            Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

            // A failed attempt shakes the field.
            transform: Translate { id: shake }
            SequentialAnimation {
                id: shakeAnim
                NumberAnimation { target: shake; property: "x"; to: -10; duration: 50 }
                NumberAnimation { target: shake; property: "x"; to: 10; duration: 50 }
                NumberAnimation { target: shake; property: "x"; to: -6; duration: 50 }
                NumberAnimation { target: shake; property: "x"; to: 6; duration: 50 }
                NumberAnimation { target: shake; property: "x"; to: 0; duration: 50 }
            }
            Connections {
                target: LockService
                function onFlash() { shakeAnim.restart(); field.revealPassword = false; }
                function onUnlockRequested() { field.revealPassword = false; }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.scaled(16)
                anchors.rightMargin: Theme.scaled(8)
                spacing: Theme.scaled(8)

                Text {
                    text: LockService.authenticating ? "󰔟" : "󰌾"
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.scaled(18)
                    color: Colors.on_surface_variant
                    RotationAnimation on rotation { running: LockService.authenticating; from: 0; to: 360; duration: 1000; loops: Animation.Infinite }
                }

                // Password input: dots or cleartext
                Item {
                    id: inputContainer
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        visible: LockService.buffer.length === 0
                        text: "Enter password"
                        color: Colors.on_surface_variant
                        font.pixelSize: Theme.scaled(14)
                    }

                    // Dots (when not revealed)
                    Row {
                        id: dotsRow
                        anchors.verticalCenter: parent.verticalCenter
                        x: Math.min(0, inputContainer.width - width)
                        spacing: Theme.scaled(5)
                        visible: !field.revealPassword && LockService.buffer.length > 0
                        Repeater {
                            model: LockService.buffer.length
                            Rectangle {
                                width: Theme.scaled(9); height: width; radius: width / 2
                                color: Theme.text
                                scale: 0
                                Component.onCompleted: scale = 1
                                Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack } }
                            }
                        }
                    }

                    // Cleartext (when revealed via eye button)
                    Text {
                        id: clearText
                        anchors.verticalCenter: parent.verticalCenter
                        x: Math.min(0, inputContainer.width - width)
                        width: implicitWidth
                        visible: field.revealPassword && LockService.buffer.length > 0
                        text: LockService.buffer
                        color: Theme.text
                        font.pixelSize: Theme.scaled(15)
                        font.family: "JetBrains Mono"
                        font.weight: Font.DemiBold
                        elide: Text.ElideNone
                    }
                }

                // Eye toggle button (show/hide password)
                Rectangle {
                    visible: LockService.buffer.length > 0
                    Layout.preferredWidth: Theme.scaled(32)
                    Layout.preferredHeight: Theme.scaled(32)
                    implicitWidth: Theme.scaled(32)
                    implicitHeight: Theme.scaled(32)
                    radius: width / 2
                    color: eyeMouse.containsMouse ? Qt.alpha(Colors.primary, 0.15) : "transparent"
                    scale: eyeMouse.pressed ? 0.92 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.animInstant } }
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }

                    Text {
                        anchors.centerIn: parent
                        text: field.revealPassword ? "󰈉" : "󰈈"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(16)
                        color: field.revealPassword ? Colors.primary : (eyeMouse.containsMouse ? Theme.text : Colors.on_surface_variant)
                    }

                    MouseArea {
                        id: eyeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: field.revealPassword = !field.revealPassword
                    }
                }

                // Submit button
                Rectangle {
                    Layout.preferredWidth: Theme.scaled(34)
                    Layout.preferredHeight: Theme.scaled(34)
                    implicitWidth: Theme.scaled(34)
                    implicitHeight: Theme.scaled(34)
                    radius: width / 2
                    color: LockService.buffer.length > 0 ? Colors.primary : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    Text {
                        anchors.centerIn: parent
                        text: "󰌑"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(16)
                        color: LockService.buffer.length > 0 ? Colors.on_primary : Colors.on_surface_variant
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: LockService.buffer.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: LockService.submit()
                    }
                }
            }
        }

        // State message
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: {
                if (LockService.message !== "") return LockService.message;
                switch (LockService.state) {
                case LockService.State.Failed: return "Incorrect password";
                case LockService.State.Error: return "Authentication error";
                case LockService.State.MaxTries: return "Too many attempts";
                }
                return LockService.authenticating ? "Checking…" : (CaffeineService.active ? "Caffeine is on" : "");
            }
            color: (LockService.state === LockService.State.None) ? Colors.on_surface_variant : Colors.error
            font.pixelSize: Theme.scaled(12)
            opacity: text === "" ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
        }

        Item { Layout.fillHeight: true }

        SystemClock { id: clock; precision: SystemClock.Minutes }
    }

    // ---- Right: resources, battery ---------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: Theme.scaled(12)

        Rectangle {
            id: resCard
            Layout.fillWidth: true
            implicitHeight: resCol.implicitHeight + Theme.scaled(28)
            radius: root.panelRadius
            color: root.panelColor

            ColumnLayout {
                id: resCol
                anchors.fill: parent
                anchors.margins: Theme.scaled(14)
                spacing: Theme.scaled(12)

                // Header
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: "󰍛"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(16)
                        color: Colors.primary
                    }

                    Text {
                        text: "SYSTEM USAGE"
                        color: Theme.subtext0
                        font.pixelSize: Theme.scaled(10)
                        font.weight: Font.Black
                        font.letterSpacing: 1
                    }

                    Item { Layout.fillWidth: true }
                }

                // 2x2 Grid of Metrics
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: Theme.scaled(8)
                    columnSpacing: Theme.scaled(8)

                    Repeater {
                        model: [
                            { key: "cpu", icon: "󰍛", label: "CPU", unit: "%", col: Theme.cpuColor, max: 100 },
                            { key: "mem", icon: "󰘚", label: "Memory", unit: "%", col: Theme.memColor, max: 100 },
                            { key: "temp", icon: "󰔏", label: "Temp", unit: "°C", col: Theme.tempColor, max: 100 },
                            { key: "fs", icon: "󰋊", label: "Disk", unit: "%", col: Colors.secondary, max: 100 }
                        ]

                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: metricCol.implicitHeight + Theme.scaled(14)
                            radius: Theme.scaled(12)
                            color: Qt.alpha(Colors.surface_container_high, 0.6)
                            border.color: Theme.glassBorder
                            border.width: 1

                            readonly property real liveValue: {
                                if (modelData.key === "cpu") return ResourceService.cpu;
                                if (modelData.key === "mem") return ResourceService.mem;
                                if (modelData.key === "temp") return ResourceService.temp;
                                if (modelData.key === "fs") return ResourceService.fs;
                                return 0;
                            }

                            ColumnLayout {
                                id: metricCol
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(10)
                                spacing: Theme.scaled(6)

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.scaled(6)

                                    Text {
                                        text: modelData.icon
                                        font.family: Theme.iconFont
                                        font.pixelSize: Theme.scaled(20)
                                        color: modelData.col
                                    }

                                    Text {
                                        text: modelData.label
                                        color: Theme.subtext0
                                        font.pixelSize: Theme.scaled(11)
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }

                                Text {
                                    text: liveValue + modelData.unit
                                    color: Theme.text
                                    font.pixelSize: Theme.scaled(16)
                                    font.weight: Font.Bold
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: Theme.scaled(4)
                                    radius: height / 2
                                    color: Qt.alpha(Colors.surface_container_highest, 0.8)

                                    Rectangle {
                                        width: parent.width * Math.max(0, Math.min(1, liveValue / modelData.max))
                                        height: parent.height
                                        radius: height / 2
                                        color: modelData.col
                                        Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: batteryCard
            Layout.fillWidth: true
            implicitHeight: batCol.implicitHeight + Theme.scaled(28)
            radius: root.panelRadius
            color: root.panelColor
            visible: HardwareService.hasBattery

            readonly property int batPercent: Math.max(0, Math.min(100, BatteryService.percentage))
            readonly property string batState: BatteryService.status
            readonly property bool acOnline: BatteryService.acOnline

            function batteryIcon(p, state, ac) {
                const isLimitActive = (state === "not charging" || state === "full" || state === "idle") && ac;
                if (isLimitActive) return "";
                if (state === "charging") return "󰂄";
                if (p >= 90) return "󰁹";
                if (p >= 75) return "󰂁";
                if (p >= 60) return "󰁿";
                if (p >= 40) return "󰁽";
                if (p >= 20) return "󰁻";
                return "󰂎";
            }

            function batteryColor(p, state, ac) {
                if (state === "charging") return Theme.powerGreen;
                if (p <= 15) return Theme.red;
                return Theme.fontColor;
            }

            function batteryStatusText(p, state, ac) {
                const isLimitActive = (state === "not charging" || state === "full" || state === "idle") && ac;
                if (isLimitActive) return "Plugged in (Conservation)";
                if (state === "charging") {
                    if (BatteryService.timeRemaining && BatteryService.timeRemaining !== "" && BatteryService.timeRemaining !== "Calculating...") {
                        return "Charging • " + BatteryService.timeRemaining;
                    }
                    return "Charging";
                }
                if (ac) return "Plugged in";
                if (BatteryService.timeRemaining && BatteryService.timeRemaining !== "" && BatteryService.timeRemaining !== "Calculating...") {
                    return BatteryService.timeRemaining + " left";
                }
                return state === "discharging" ? "Discharging" : (state ? state.charAt(0).toUpperCase() + state.slice(1) : "Battery");
            }

            ColumnLayout {
                id: batCol
                anchors.fill: parent
                anchors.margins: Theme.scaled(14)
                spacing: Theme.scaled(10)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    Text {
                        text: batteryCard.batteryIcon(batteryCard.batPercent, batteryCard.batState, batteryCard.acOnline)
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(28)
                        color: batteryCard.batteryColor(batteryCard.batPercent, batteryCard.batState, batteryCard.acOnline)

                        SequentialAnimation on opacity {
                            running: batteryCard.batState === "charging"
                            loops: Animation.Infinite
                            NumberAnimation { from: 1.0; to: 0.35; duration: 600; easing.type: Easing.InOutQuad }
                            NumberAnimation { from: 0.35; to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
                        }
                    }

                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: batteryCard.batPercent + "%"
                                color: Theme.text
                                font.pixelSize: Theme.scaled(18)
                                font.weight: Font.Bold
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: batteryCard.batState === "charging" ? "⚡ CHARGING" : (batteryCard.acOnline ? "AC CONNECTED" : "BATTERY")
                                color: batteryCard.batState === "charging" ? Theme.powerGreen : Theme.subtext0
                                font.pixelSize: Theme.scaled(10)
                                font.weight: Font.Black
                                font.letterSpacing: 1
                            }
                        }
                        Text {
                            text: batteryCard.batteryStatusText(batteryCard.batPercent, batteryCard.batState, batteryCard.acOnline)
                            color: Theme.subtext0
                            font.pixelSize: Theme.scaled(12)
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }

                // Battery Progress Bar
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Theme.scaled(6)
                    radius: height / 2
                    color: Qt.alpha(Colors.surface_container_highest, 0.9)

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, batteryCard.batPercent / 100))
                        height: parent.height
                        radius: height / 2
                        color: batteryCard.batteryColor(batteryCard.batPercent, batteryCard.batState, batteryCard.acOnline)
                        Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
                    }
                }
            }
        }

        Rectangle {
            id: notifCard
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: root.panelRadius
            color: root.panelColor
            clip: true

            readonly property int notifCount: NotificationService.notifications.count

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(16)
                spacing: Theme.scaled(12)

                // Header Row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: "󰂚"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(16)
                        color: Colors.primary
                    }

                    Text {
                        text: "NOTIFICATIONS"
                        color: Theme.subtext0
                        font.pixelSize: Theme.scaled(10)
                        font.weight: Font.Black
                        font.letterSpacing: 1
                    }

                    Item { Layout.fillWidth: true }

                    // Count Badge or All Caught Up Tag
                    Rectangle {
                        implicitWidth: countBadgeText.implicitWidth + Theme.scaled(14)
                        implicitHeight: Theme.scaled(20)
                        radius: height / 2
                        color: notifCard.notifCount > 0 ? Qt.alpha(Colors.primary, 0.18) : Qt.alpha(Colors.surface_container_highest, 0.5)
                        border.color: notifCard.notifCount > 0 ? Qt.alpha(Colors.primary, 0.4) : Theme.glassBorder
                        border.width: 1

                        Text {
                            id: countBadgeText
                            anchors.centerIn: parent
                            text: notifCard.notifCount > 0 ? (notifCard.notifCount + " new") : "0"
                            color: notifCard.notifCount > 0 ? Colors.primary : Theme.subtext0
                            font.pixelSize: Theme.scaled(10)
                            font.weight: Font.Bold
                        }
                    }

                    // Clear all button (only when notifications exist)
                    Rectangle {
                        visible: notifCard.notifCount > 0
                        implicitWidth: Theme.scaled(22)
                        implicitHeight: Theme.scaled(22)
                        radius: width / 2
                        color: clearAllM.containsMouse ? Qt.alpha(Colors.error, 0.2) : Qt.alpha(Colors.surface_container_highest, 0.5)
                        border.color: Theme.glassBorder
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "󰆴"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(11)
                            color: clearAllM.containsMouse ? Colors.error : Theme.subtext0
                        }

                        MouseArea {
                            id: clearAllM
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: NotificationService.clearAll()
                        }
                    }
                }

                // Notification Items List
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    visible: notifCard.notifCount > 0

                    Repeater {
                        model: Math.min(notifCard.notifCount, 3)

                        Rectangle {
                            id: itemRect
                            required property int index
                            readonly property var n: NotificationService.notifications.get(index)

                            Layout.fillWidth: true
                            implicitHeight: itemRow.implicitHeight + Theme.scaled(14)
                            radius: Theme.scaled(12)
                            color: itemM.containsMouse ? Qt.alpha(Colors.surface_container_highest, 0.75) : Qt.alpha(Colors.surface_container_high, 0.55)
                            border.color: itemM.containsMouse ? Qt.alpha(Colors.primary, 0.35) : Theme.glassBorder
                            border.width: 1
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }

                            RowLayout {
                                id: itemRow
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(8)
                                spacing: Theme.scaled(10)

                                // App Icon / Initials Badge
                                Rectangle {
                                    implicitWidth: Theme.scaled(28)
                                    implicitHeight: Theme.scaled(28)
                                    radius: Theme.scaled(8)
                                    color: Qt.alpha(Colors.primary, 0.18)
                                    border.color: Qt.alpha(Colors.primary, 0.3)
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: (n && n.appName && n.appName.length > 0) ? n.appName.charAt(0).toUpperCase() : "󰂚"
                                        font.pixelSize: Theme.scaled(12)
                                        font.weight: Font.Bold
                                        color: Colors.primary
                                        font.family: (n && n.appName && n.appName.length > 0) ? "" : Theme.iconFont
                                    }
                                }

                                // Details: App name, summary, body preview
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            text: n ? (n.appName || "Notification") : ""
                                            color: Colors.primary
                                            font.pixelSize: Theme.scaled(11)
                                            font.weight: Font.Bold
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }

                                    Text {
                                        text: n ? (n.summary || "") : ""
                                        color: Theme.text
                                        font.pixelSize: Theme.scaled(12)
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: n ? (n.body || "") : ""
                                        color: Theme.subtext0
                                        font.pixelSize: Theme.scaled(10)
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        visible: n && n.body && n.body !== ""
                                    }
                                }

                                // Dismiss button
                                Rectangle {
                                    implicitWidth: Theme.scaled(20)
                                    implicitHeight: Theme.scaled(20)
                                    radius: width / 2
                                    color: disM.containsMouse ? Qt.alpha(Colors.error, 0.25) : "transparent"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "󰅖"
                                        font.family: Theme.iconFont
                                        font.pixelSize: Theme.scaled(11)
                                        color: disM.containsMouse ? Colors.error : Theme.subtext0
                                    }

                                    MouseArea {
                                        id: disM
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (itemRect.n && itemRect.n.id !== undefined) {
                                                NotificationService.dismissNotification(itemRect.n.id);
                                            }
                                        }
                                    }
                                }
                            }

                            MouseArea {
                                id: itemM
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }
                    }

                    Text {
                        text: "+ " + (notifCard.notifCount - 3) + " more"
                        color: Theme.subtext0
                        font.pixelSize: Theme.scaled(10)
                        font.weight: Font.Medium
                        Layout.alignment: Qt.AlignHCenter
                        visible: notifCard.notifCount > 3
                    }
                }

                // Empty State (All caught up)
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: notifCard.notifCount === 0
                    Layout.alignment: Qt.AlignCenter
                    spacing: Theme.scaled(8)

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: Theme.scaled(44)
                        implicitHeight: Theme.scaled(44)
                        radius: width / 2
                        color: Qt.alpha(Colors.surface_container_highest, 0.45)
                        border.color: Theme.glassBorder
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "󰂚"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(20)
                            color: Theme.subtext0
                            opacity: 0.7
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "All Caught Up"
                        color: Theme.text
                        font.pixelSize: Theme.scaled(13)
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "No new notifications"
                        color: Theme.subtext0
                        font.pixelSize: Theme.scaled(11)
                        opacity: 0.75
                    }
                }

                Item { Layout.fillHeight: true; visible: notifCard.notifCount > 0 }
            }
        }
    }
}
