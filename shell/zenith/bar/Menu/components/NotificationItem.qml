import QtQuick
import QtQuick.Controls 2.15
import QtQuick.Layouts
import Quickshell
import Quickshell.Services.Notifications
import "../../../services"
import "../../../windows" as Win
import "../../../"

Rectangle {
    id: root

    property var notification: null
    property bool enableAutoDismiss: true
    property bool animateHeight: true
    property int itemIndex: 0
    
    // Stability: Debounced hovered state
    property bool realHovered: false
    Timer {
        id: unhoverTimer
        interval: 250
        onTriggered: {
            if (!mainMouseArea.containsMouse && !dismissMouse.containsMouse) {
                root.realHovered = false;
                autoDismissTimer.restart();
            }
        }
    }

    // --- ZENITH THEMEING ---
    color: enableAutoDismiss 
        ? Theme.glassBackground 
        : (mainMouseArea.containsMouse ? Theme.cardBackgroundHover : Theme.cardBackground)
    radius: Theme.scaled(14)
    border.color: enableAutoDismiss 
        ? Theme.glassBorder 
        : (mainMouseArea.containsMouse ? Theme.glassBorder : Theme.cardBorder)
    border.width: 1
    clip: true

    implicitHeight: layout.implicitHeight + Theme.scaled(24)
    Layout.fillWidth: true

    // --- ANIMATIONS ---
    opacity: 0
    scale: 0.92
    transform: Translate { id: trans; x: 50 }

    Component.onCompleted: {
        appearAnim.start();
        updateCandidates();
    }

    ParallelAnimation {
        id: appearAnim
        NumberAnimation { target: root; property: "opacity"; to: 1; duration: Theme.animNormal; easing.type: Theme.animEasing }
        NumberAnimation { target: root; property: "scale"; to: 1.0; duration: Theme.animNormal; easing.type: Theme.elasticEasing }
        NumberAnimation { target: trans; property: "x"; to: 0; duration: Theme.animSlow; easing.type: Theme.elasticEasing }
    }

    // Leaving mirrors arriving: slide out to the right and fade, then let the
    // model drop the row. Removing the row first made popups blink out of
    // existence while everything else in the shell animates.
    property bool _leaving: false
    signal leaveFinished()

    function leave() {
        if (_leaving) return;
        _leaving = true;
        appearAnim.stop();
        leaveAnim.start();
    }

    SequentialAnimation {
        id: leaveAnim
        ParallelAnimation {
            NumberAnimation { target: root; property: "opacity"; to: 0; duration: Theme.animFast; easing.type: Easing.InCubic }
            NumberAnimation { target: root; property: "scale"; to: 0.94; duration: Theme.animFast; easing.type: Easing.InCubic }
            NumberAnimation { target: trans; property: "x"; to: 60; duration: Theme.animFast; easing.type: Easing.InCubic }
        }
        ScriptAction { script: root.leaveFinished() }
    }

    // Popup rows animate out; the dashboard history list (animateOut false)
    // removes rows directly, since it is a scrolling list rather than a stack
    // of toasts.
    property bool animateOut: true

    function dismiss(removeFromHistory) {
        if (!notification) return;
        const id = notification.id;
        const finish = () => {
            if (removeFromHistory) NotificationService.removeNotification(id);
            else NotificationService.dismissNotification(id);
        };
        if (!animateOut) { finish(); return; }
        leaveFinished.connect(finish);
        leave();
    }

    Behavior on implicitHeight {
        enabled: root.animateHeight
        NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic }
    }

    // --- AUTO DISMISS LOGIC ---
    Timer {
        id: autoDismissTimer
        interval: 3000
        running: !!notification && enableAutoDismiss
        repeat: false
        onTriggered: {
            if (!root.realHovered) root.dismiss(false);
        }
    }

    // --- DYNAMIC ICON RESOLUTION & WATERFALL ---
    property var iconCandidates: []
    property int currentCandidateIndex: -1
    property bool showFallbackLetter: false

    function tryNextIcon() {
        currentCandidateIndex++;
        if (currentCandidateIndex < iconCandidates.length) {
            let nextSource = iconCandidates[currentCandidateIndex];
            if (nextSource && nextSource !== "") {
                iconImg.source = nextSource;
            } else {
                Qt.callLater(tryNextIcon);
            }
        } else {
            // All candidates exhausted, show letter fallback
            showFallbackLetter = true;
        }
    }

    function updateCandidates() {
        if (!notification) return;
        showFallbackLetter = false;

        let candidates = [];
        if (notification.iconCandidates && Array.isArray(notification.iconCandidates) && notification.iconCandidates.length > 0) {
            candidates = notification.iconCandidates;
        } else {
            candidates = Win.IconsFetcher.getIconCandidates(notification.appName || "", notification.desktopEntry || "", notification.rawIcon || notification.appIcon || "");
        }

        iconCandidates = candidates.filter((v, i, a) => v && v !== "" && a.indexOf(v) === i);
        currentCandidateIndex = -1;
        tryNextIcon();
    }

    onNotificationChanged: updateCandidates()

    // --- Main layout ---
    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.scaled(12)
        spacing: Theme.scaled(8)
        z: 2
        
        Layout.alignment: Qt.AlignVCenter 

        // Icon Container
        Rectangle {
            id: iconContainer
            Layout.preferredWidth: Theme.scaled(50)
            Layout.preferredHeight: Theme.scaled(50)
            Layout.alignment: Qt.AlignVCenter 
            
            color: "transparent"
            radius: Theme.scaled(12)
            border.width: 0

            Image {
                id: iconImg
                anchors.centerIn: parent
                
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                visible: !root.showFallbackLetter
                
                onStatusChanged: {
                    if (status === Image.Ready) {
                        // Detect Qt's 100x100 missing icon checkerboard pattern
                        if (iconImg.source.toString().startsWith("image://icon/") &&
                            iconImg.implicitWidth === 100 && iconImg.implicitHeight === 100) {
                            let iconName = iconImg.source.toString().replace("image://icon/", "");
                            if (!Quickshell.hasThemeIcon(iconName)) {
                                Qt.callLater(tryNextIcon);
                            }
                        }
                    } else if (status === Image.Error) {
                        Qt.callLater(tryNextIcon);
                    }
                }
            }

            // Fallback Letter Container
            Text {
                anchors.centerIn: parent
                visible: root.showFallbackLetter
                text: (notification && notification.appName && notification.appName !== "") ? notification.appName.charAt(0).toUpperCase() : "!"
                font.pixelSize: Theme.scaled(24)
                font.bold: true
                color: Theme.accentColor
            }
        }

        // Text Section
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.scaled(2)

            Label {
                text: notification ? (notification.appName || "SYSTEM").toUpperCase() : ""
                color: Theme.accentColor
                font.pixelSize: Theme.scaled(10)
                font.weight: Font.Black
                font.letterSpacing: 1.5
                Layout.fillWidth: true
            }

            Label {
                text: notification ? (notification.summary || "Notification") : ""
                color: "#ffffff"
                font.bold: true
                font.pixelSize: Theme.scaled(13)
                elide: root.realHovered ? Text.ElideNone : Text.ElideRight
                wrapMode: root.realHovered ? Text.Wrap : Text.NoWrap
                Layout.fillWidth: true
            }

            Label {
                text: notification ? (notification.body || "") : ""
                color: Theme.subtext0
                font.pixelSize: Theme.scaled(11)
                wrapMode: root.realHovered ? Text.Wrap : Text.NoWrap
                elide: root.realHovered ? Text.ElideNone : Text.ElideRight
                maximumLineCount: root.realHovered ? 20 : 2
                Layout.fillWidth: true
            }
        }

        Item {
            Layout.preferredWidth: root.realHovered ? Theme.scaled(42) : Theme.scaled(10)
            Layout.minimumWidth: Layout.preferredWidth
        }
    }

    // Main Hover & Click Area
    MouseArea {
        id: mainMouseArea
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        z: 5
        
        onEntered: {
            unhoverTimer.stop();
            root.realHovered = true;
            autoDismissTimer.stop();
        }
        
        onExited: unhoverTimer.restart();

        onClicked: {
            if (notification?.originalNotif) {
                notification.originalNotif.invokeAction("default");
                notification.originalNotif.dismiss();
                root.dismiss(false);
            }
        }
    }

    // Dismiss Button
    Item {
        id: dismissButton
        width: Theme.scaled(32); height: Theme.scaled(32)
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: Theme.scaled(10)
        z: 100
        visible: root.realHovered

        Rectangle {
            anchors.fill: parent
            radius: Theme.scaled(8)
            color: dismissMouse.containsMouse ? Theme.menuDarkHover : "transparent"
            border.width: 0
            scale: dismissMouse.pressed ? 0.92 : 1.0
            Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: Theme.animInstant } }
            
            Text {
                anchors.centerIn: parent
                text: "󰅖"
                color: dismissMouse.containsMouse ? Theme.powerRed : Theme.subtext0
                font.pixelSize: Theme.scaled(20)
            }
        }

        MouseArea {
            id: dismissMouse
            anchors.fill: parent
            hoverEnabled: true
            onEntered: {
                unhoverTimer.stop();
                root.realHovered = true;
            }
            onExited: unhoverTimer.restart();
            onClicked: {
                if (notification) {
                    notification.originalNotif?.dismiss();
                    root.dismiss(true);
                }
            }
        }
    }
}
