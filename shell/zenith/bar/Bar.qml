// bar/Bar.qml
import ".."
import "./Menu"
import "./Right"
import "../services"
import "../Settings"
import QtQuick
import qs.services as Cae
import QtQuick.Layouts
import Quickshell
import Quickshell.Hyprland
import Quickshell.Wayland

PanelWindow {
    id: bar


    WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand
    WlrLayershell.layer: WlrLayer.Top
    exclusionMode: ExclusionMode.Auto
    margins {
        top: Theme.barMarginTop
        bottom: Theme.barMarginBottom
        left: Theme.barMarginLeft
        right: Theme.barMarginRight
    }

    anchors {
        top: true
        left: true
        right: true
    }

    implicitHeight: Theme.barHeight
    implicitWidth: screen ? screen.width : Theme.screenWidth
    color: "transparent"
    visible: !HyprlandService.isFullscreen && !LockService.locked

    Rectangle {
        id: barVisual

        anchors.fill: parent

        color: Theme.barColor
        radius: Theme.barRadius || 0
        clip: true
        opacity: BarSettings.entryAnimation ? 0 : 1
        y: BarSettings.entryAnimation ? -height : 0

        ParallelAnimation {
            id: barEntryAnim

            NumberAnimation {
                target: barVisual
                property: "y"
                to: 0
                duration: Theme.animFast
                easing.type: Easing.OutCubic
            }

            NumberAnimation {
                target: barVisual
                property: "opacity"
                to: 1
                duration: Theme.animFast
                easing.type: Easing.OutCubic
            }
        }

        // --- DISMISS ON BAR CLICK ---
        MouseArea {
            anchors.fill: parent
            z: -1 // Bottom of stack, handles clicks on empty space
            // Ensure we capture clicks even on transparent background
            onPressed: (mouse) => mouse.accepted = true
            onClicked: {
                MenuService.closeAll();
            }
        }

        Component.onCompleted: {
            if (BarSettings.entryAnimation) {
                barEntryAnim.start();
                contentFadeAnim.start();
            }
        }
        
        NumberAnimation {
            id: contentFadeAnim
            targets: [leftSide, centerSide, rightLayout]
            property: "opacity"
            from: 0; to: 1
            duration: Theme.animNormal
            easing.type: Easing.OutCubic
        }

        // --- LEFT SIDE ---
        Left {
            id: leftSide
            anchors.left: parent.left
            anchors.leftMargin: Theme.barMarginLeft
            anchors.verticalCenter: parent.verticalCenter
            height: Theme.pillHeight
            opacity: BarSettings.entryAnimation ? 0 : 1
        }

        // --- PERFECT CENTER ---
        Center {
            id: centerSide
            anchors.verticalCenter: parent.verticalCenter
            height: Theme.pillHeight
            opacity: BarSettings.entryAnimation ? 0 : 1
            
            x: {
                let screenW = (screen ? screen.width : Theme.screenWidth);
                let absoluteCenter = screenW / 2;
                let marginLeft = Theme.barMarginLeft;
                
                // Calculate position relative to the bar window
                let preferredX = (absoluteCenter - marginLeft) - (width / 2);
                
                let leftBound = leftSide.x + leftSide.width + Theme.pillGap;
                let rightBound = rightLayout.x - width - Theme.pillGap;
                
                // If it can fit in the absolute center, put it there.
                // If the right side is pushing it, move it left.
                // But don't let it overlap the left side.
                return Math.max(leftBound, Math.min(preferredX, rightBound));
            }

            // Hide only if the available space is smaller than the widget itself
            visible: {
                let availableSpace = rightLayout.x - (leftSide.x + leftSide.width) - (Theme.pillGap * 2);
                return width <= availableSpace;
            }
        }

        // --- RIGHT SIDE ---
        RowLayout {
            id: rightLayout
            anchors.right: parent.right
            anchors.rightMargin: Theme.barMarginRight
            anchors.verticalCenter: parent.verticalCenter
            height: Theme.pillHeight
            spacing: Theme.pillSpacing
            opacity: BarSettings.entryAnimation ? 0 : 1

            Repeater { // Windows: Mix & match pieces from the other shells
                model: Cae.WinSettings.barPieces ?? []
                delegate: Loader {
                    required property var modelData
                    Layout.alignment: Qt.AlignVCenter
                    source: Qt.resolvedUrl("../../modules/bar/pieces/MixPiece.qml")
                    onLoaded: { item.vertical = false; item.screen = Qt.binding(() => bar.screen); item.pieceId = String(modelData); }
                }
            }

            Tray { menuRef: trayPopup }
            
            Network {
                id: wifiWidget
                visible: WidgetSettings.enableNetwork && HardwareService.hasNetworking
            }
            
            Resources { 
                id: resourcesWidget
                visible: WidgetSettings.enableResources && !Theme.isSmallScreen
            }
            
            QuickSettingsCluster {
                id: quickSettingsCluster
            }
        }
    }

    TrayMenu {
        id: trayPopup
        anchor.window: bar
    }
}
