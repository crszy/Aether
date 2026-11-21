import "../../../services"
import "../../../Settings"
import QtQuick
import QtQuick.Controls 2.15
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland
import "../../.."

PanelWindow {
    id: popupStack

    readonly property bool useFullscreenLayout: NotificationSettings.fullscreenNotification
    readonly property bool isFullscreen: HyprlandService.isFullscreen

    // Fullscreen keeps notifications out of the way by tucking them up under
    // the screen edge, leaving a strip visible. Hovering that strip slides the
    // stack down so the whole notification can be read, and it tucks itself
    // back on leave.
    //
    // The tuck is applied to the content inside a fixed-size surface, not to
    // the layer-shell margin. The margin used to be `- bar.height`, which moved
    // the surface itself off-screen: the hidden part was then unreachable, and
    // animating a layer-shell margin means a Wayland reconfigure every frame.
    readonly property int tuckDepth: Theme.scaled(58)
    readonly property bool tucked: isFullscreen && !(osdLoader.item?.visible ?? false)
    property bool stackHovered: false


    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

    // Standard popup position (top-right)
    anchors {
        top: true
        right: true
    }

    margins {
        top: popupStack.isFullscreen ? ((osdLoader.item?.visible ?? false) ? Theme.scaled(40) : 0) : ((osdLoader.item?.visible ?? false) ? Theme.scaled(105) : Theme.scaled(10))
        right: popupStack.isFullscreen ? Theme.scaled(5) : Theme.scaled(10)
    }

    implicitWidth: Theme.scaled(400)
    // Use a stable height to avoid Wayland resize overhead during hover expansion
    implicitHeight: activeNotifications.count > 0 ? Theme.scaled(800) : 0
    
    visible: !LockService.locked && activeNotifications.count > 0 && (!popupStack.isFullscreen || popupStack.useFullscreenLayout)
    color: "transparent"

    // Only capture input where notifications actually are
    mask: Region {
        item: mainColumn
    }

    ListModel {
        id: activeNotifications
    }

    // The layout remains "the same"
    ColumnLayout {
        id: mainColumn
        width: Theme.scaled(400)
        spacing: Theme.scaled(10)

        y: (popupStack.tucked && !popupStack.stackHovered) ? -popupStack.tuckDepth : 0
        Behavior on y {
            NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic }
        }

        // Covers the stack including the visible strip while tucked, so the
        // sliver is what you hover to pull the rest down.
        HoverHandler {
            id: stackHover
            onHoveredChanged: popupStack.stackHovered = hovered
        }

        Repeater {
            model: activeNotifications
            delegate: NotificationItem {
                notification: activeNotifications.get(index)
                Layout.fillWidth: true
            }
        }
    }

    Connections {
        function onNotificationReceived(notifData) {
            activeNotifications.append(notifData);
        }

        function onNotificationDismissed(id) {
            for (let i = 0; i < activeNotifications.count; i++) {
                if (activeNotifications.get(i).id === id) {
                    activeNotifications.remove(i);
                    break;
                }
            }
        }

        target: NotificationService
    }
}
