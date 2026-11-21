import QtQuick
import QtQuick.Controls 2.15
import QtQuick.Layouts
import Quickshell
import Quickshell.Services.Mpris
import "../../../services"
import "../../../"

Rectangle {
    id: mprisPlayer
    color: Theme.cardBackground
    radius: Theme.bubbleRadiusMedium
    border.color: Theme.glassBorder
    border.width: 1
    Layout.fillWidth: true
    implicitHeight: Theme.scaled(110)

    // --- State & Logic ---
    property bool active: false
    readonly property var player: MediaPlayerService.trackedPlayer
    
    // Bindings to unified service state
    readonly property real currentPos: MediaPlayerService.currentPos
    readonly property bool isActuallyPlaying: MediaPlayerService.isActuallyPlaying

    property bool switcherHovered: false
    readonly property bool isPlayerHovered: bgHover.hovered || posSlider.hovered || posSlider.pressed || prevBtn.hovered || prevBtn.pressed || playBtn.hovered || playBtn.pressed || nextBtn.hovered || nextBtn.pressed || switcherHovered

    HoverHandler {
        id: bgHover
    }

    function formatTime(s) {
        if (s < 0 || isNaN(s)) return "0:00"
        let mins = Math.floor(s / 60); let secs = Math.floor(s % 60)
        return mins + ":" + (secs < 10 ? "0" : "") + secs
    }

    // --- UI Layout ---
    RowLayout {
        anchors.centerIn: parent
        width: parent.width - Theme.scaled(24)
        spacing: Theme.scaled(15)

        Rectangle {
            width: Theme.isSmallScreen ? Theme.scaled(60) : Theme.scaled(80)
            height: width
            radius: Theme.bubbleRadiusSmall; color: "transparent"; border.color: Theme.glassBorder; border.width: 1; clip: true
            Layout.alignment: Qt.AlignVCenter
            Image {
                anchors.fill: parent
                source: player ? String(player.trackArtUrl || "") : ""
                fillMode: Image.PreserveAspectCrop
                opacity: (player && player.trackArtUrl && status === Image.Ready) ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 250 } }
            }
            Text { 
                anchors.centerIn: parent; text: "󰎆"; color: Theme.subtext0; font.pixelSize: Theme.isSmallScreen ? Theme.scaled(20) : Theme.scaled(28)
                visible: !player || !player.trackArtUrl 
            }
        }

        ColumnLayout {
            Layout.fillWidth: true; spacing: Theme.scaled(2)
            Layout.alignment: Qt.AlignVCenter
            
            Label {
                text: player ? MediaPlayerService.formatMediaTitle(String(player.trackTitle || "Media"), player.identity) : "Idle"
                color: Theme.text; font.bold: true; font.pixelSize: Theme.scaled(13); elide: Text.ElideRight; Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true; spacing: 0
                Slider {
                    id: posSlider; Layout.fillWidth: true; 
                    Layout.preferredHeight: Theme.scaled(18)
                    from: 0; to: (player && player.length > 0) ? player.length : 100
                    value: mprisPlayer.currentPos
                    onMoved: { if(player) player.position = value }
                    padding: 0; leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
                    background: Rectangle {
                        x: posSlider.leftPadding + Theme.scaled(6); y: posSlider.topPadding + (posSlider.availableHeight - height) / 2
                        height: Theme.scaled(4); width: posSlider.availableWidth - Theme.scaled(12); radius: Theme.scaled(2); color: Qt.rgba(255, 255, 255, 0.08); border.color: Theme.glassBorder; border.width: 1
                        Rectangle { width: posSlider.visualPosition * parent.width; height: Theme.scaled(4); color: Theme.accentColor; radius: Theme.scaled(2) }
                    }
                    handle: Rectangle {
                        x: posSlider.leftPadding + posSlider.visualPosition * (posSlider.availableWidth - width)
                        y: posSlider.topPadding + (posSlider.availableHeight - height) / 2
                        width: Theme.scaled(10); height: Theme.scaled(10); radius: Theme.scaled(5); color: Theme.accentColor
                        visible: posSlider.hovered || posSlider.pressed
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: mprisPlayer.formatTime(mprisPlayer.currentPos); color: Theme.subtext1; font.pixelSize: Theme.scaled(9) }
                    Item { Layout.fillWidth: true }
                    Label { text: mprisPlayer.formatTime(player ? player.length : 0); color: Theme.subtext1; font.pixelSize: Theme.scaled(9) }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                RowLayout {
                    spacing: Theme.scaled(6)
                    Button { 
                        id: prevBtn
                        flat: true
                        implicitWidth: Theme.scaled(34)
                        implicitHeight: Theme.scaled(34)
                        hoverEnabled: true
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        scale: pressed ? 0.90 : 1.0
                        Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
                        onClicked: { if(player) player.previous() }
                        background: Rectangle {
                            radius: height / 2
                            color: prevBtn.pressed 
                                ? Theme.accentColor
                                : (prevBtn.hovered ? Theme.cardBackgroundHover : "transparent")
                            border.width: 0
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        }
                        contentItem: Text { 
                            text: "󰒮"
                            color: prevBtn.hovered ? Theme.subtext1 : Theme.text
                            font.pixelSize: Theme.scaled(20)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter 
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        } 
                    }
                    Button { 
                        id: playBtn
                        flat: true
                        implicitWidth: Theme.scaled(42)
                        implicitHeight: Theme.scaled(42)
                        hoverEnabled: true
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        scale: pressed ? 0.90 : 1.0
                        Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
                        onClicked: { 
                            if(player) {
                                if (player.playPause) player.playPause();
                                else if (mprisPlayer.isActuallyPlaying) player.pause();
                                else player.play();
                            }
                        }
                        background: Rectangle {
                            radius: height / 2
                            color: playBtn.pressed 
                                ? Qt.darker(Theme.accentColor, 1.25)
                                : (playBtn.hovered ? Theme.accentColor : Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.20))
                            border.width: 0
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        }
                        contentItem: Text { 
                            text: mprisPlayer.isActuallyPlaying ? "󰏤" : "󰐊"
                            color: (playBtn.hovered || playBtn.pressed) ? Colors.on_primary : Theme.accentColor
                            opacity: playBtn.hovered ? 0.80 : 1.0
                            font.pixelSize: Theme.scaled(22)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter 
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                            Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
                        } 
                    }
                    Button { 
                        id: nextBtn
                        flat: true
                        implicitWidth: Theme.scaled(34)
                        implicitHeight: Theme.scaled(34)
                        hoverEnabled: true
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        scale: pressed ? 0.90 : 1.0
                        Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
                        onClicked: { if(player) player.next() }
                        background: Rectangle {
                            radius: height / 2
                            color: nextBtn.pressed 
                                ? Theme.accentColor
                                : (nextBtn.hovered ? Theme.cardBackgroundHover : "transparent")
                            border.width: 0
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        }
                        contentItem: Text { 
                            text: "󰒭"
                            color: nextBtn.hovered ? Theme.subtext1 : Theme.text
                            font.pixelSize: Theme.scaled(20)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter 
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        } 
                    }
                }
                
                Item { Layout.fillWidth: true }
                
                RowLayout {
                    spacing: Theme.scaled(8)
                    Repeater {
                        model: Mpris.players.values
                        delegate: MouseArea {
                            id: switchDelegate
                            width: Theme.scaled(22); height: Theme.scaled(22)
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            scale: pressed ? 0.90 : 1.0
                            Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
                            onContainsMouseChanged: mprisPlayer.switcherHovered = containsMouse
                            onClicked: MediaPlayerService.updateTrackedPlayer(modelData)
                            Rectangle {
                                anchors.fill: parent
                                radius: width / 2
                                color: switchDelegate.pressed
                                    ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.35)
                                    : (switchDelegate.containsMouse ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.18) : "transparent")
                                border.color: switchDelegate.containsMouse ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.40) : "transparent"
                                border.width: 1
                                Behavior on color { ColorAnimation { duration: Theme.animFast } }
                            }
                            Text {
                                anchors.centerIn: parent
                                font.pixelSize: Theme.scaled(13)
                                color: (mprisPlayer.player === modelData) ? Theme.accentColor : (modelData.playbackState === MprisPlaybackState.Playing ? Theme.green : Theme.subtext0)
                                opacity: switchDelegate.containsMouse ? 0.70 : 1.0
                                Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
                                text: {
                                    let id = modelData.identity.toLowerCase();
                                    if (id.includes("firefox")) return "󰈹";
                                    if (id.includes("chrom") || id.includes("zen")) return "󰊯";
                                    if (id.includes("spotify")) return "󰓇";
                                    if (id.includes("vlc")) return "󰕼";
                                    return "󰝚";
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
