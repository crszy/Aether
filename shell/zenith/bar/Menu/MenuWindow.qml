// Shared scaffolding for every menu surface.
//
// ControlCenter and QuickSettingsMenu were each carrying their own copy of:
// overlay layer, screen-filling anchors, an input mask over the card,
// register/unregister with MenuService, and a show animation. Identical code
// in several files means a mistake gets made several times -- which is what
// happened with the "dismiss on outer click" MouseArea, present and wrong in
// all of them until it was fixed in all of them.
//
// Usage:
//
//     MenuWindow {
//         id: root
//         shown: SomeService.visibleFlag   // the only thing a caller binds
//         card: mainContent                // the thing that takes input
//         namespaceName: "whatever"        // layer-shell namespace
//         onDismissed: SomeService.close()
//
//         Rectangle { id: mainContent /* ... */ }
//     }
//
// `visible` is owned here: it turns on the moment `shown` goes true and only
// turns off once the exit animation has finished, so a menu leaves the screen
// the way it arrived instead of vanishing on the frame it was dismissed.
import QtQuick
import Quickshell
import Quickshell.Wayland
import Quickshell.Hyprland
import "../.."
import "../../services"

PanelWindow {
    id: menuWindow

    // Bound by the caller to the service flag that means "this menu is open".
    property bool shown: false

    // The visible card. Input is masked to it; everything outside is not this
    // window's business.
    property Item card: null

    property string namespaceName: "zenith-menu"

    // Where the card grows out from and shrinks back into.
    property int cardOrigin: Item.Top

    // A surface that should not participate in "close all open menus" can opt
    // out; the dynamic island does its own lifecycle.
    property bool managedByMenuService: true

    // Raised when the menu should close itself -- a click outside it, or Escape.
    signal dismissed()

    // Set while the menu is in the middle of something that must not be
    // interrupted. Changing WlrLayershell.keyboardFocus reconfigures the
    // surface, and that reconfigure can clear the focus grab -- so entering a
    // WiFi password, which flips keyboardFocus to Exclusive, looked exactly
    // like a click outside and closed the whole panel.
    property bool dismissInhibited: false

    visible: false
    color: "transparent"

    WlrLayershell.layer: WlrLayer.Overlay
    exclusiveZone: 0
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand
    WlrLayershell.namespace: menuWindow.namespaceName

    anchors {
        top: true
        bottom: true
        left: true
        right: true
    }

    // Only the card receives input. Clicks anywhere else pass through to
    // DismissOverlay, which is what actually closes menus.
    //
    // Do not add a dismiss MouseArea to this window. Because of this mask the
    // compositor only delivers events that land on the card, so such a handler
    // can only ever fire *inside* the menu -- it closes the menu on every click
    // that misses a control, which is the opposite of dismissing it.
    mask: Region {
        item: menuWindow.card
    }

    // ---- Enter / exit ----------------------------------------------------

    property Translate _cardTranslate: Translate {}

    function resetCard() {
        if (!card) return;
        card.transformOrigin = menuWindow.cardOrigin;
        card.transform = [menuWindow._cardTranslate];
        card.opacity = 0;
        card.scale = 0.94;
        _cardTranslate.y = -Theme.scaled(8);
    }

    onCardChanged: resetCard()

    onCardOriginChanged: {
        if (card) card.transformOrigin = cardOrigin;
    }

    Component.onCompleted: {
        resetCard();
        if (shown) {
            visible = true;
            showAnim.restart();
        }
    }

    onShownChanged: {
        if (shown) {
            hideAnim.stop();
            resetCard();
            visible = true;
            showAnim.restart();
        } else if (visible) {
            showAnim.stop();
            hideAnim.restart();
        }
    }

    ParallelAnimation {
        id: showAnim
        NumberAnimation { target: menuWindow.card; property: "opacity"; from: 0; to: 1; duration: Theme.animFast; easing.type: Easing.OutCubic }
        NumberAnimation { target: menuWindow.card; property: "scale"; from: 0.94; to: 1.0; duration: Theme.animFast; easing.type: Easing.OutBack; easing.overshoot: 1.04 }
        NumberAnimation { target: menuWindow._cardTranslate; property: "y"; from: -Theme.scaled(8); to: 0; duration: Theme.animFast; easing.type: Easing.OutCubic }
    }

    SequentialAnimation {
        id: hideAnim
        ParallelAnimation {
            NumberAnimation { target: menuWindow.card; property: "opacity"; to: 0; duration: Math.round(Theme.animFast * 0.7); easing.type: Easing.InCubic }
            NumberAnimation { target: menuWindow.card; property: "scale"; to: 0.94; duration: Math.round(Theme.animFast * 0.7); easing.type: Easing.InCubic }
            NumberAnimation { target: menuWindow._cardTranslate; property: "y"; to: -Theme.scaled(8); duration: Math.round(Theme.animFast * 0.7); easing.type: Easing.InCubic }
        }
        ScriptAction {
            script: {
                if (!menuWindow.shown) {
                    menuWindow.visible = false;
                    menuWindow.resetCard();
                }
            }
        }
    }

    // ---- Dismissal ---------------------------------------------------------

    // Closing on an outside click is a focus grab, not a full-screen click
    // catcher. DismissOverlay only maps once a menu has registered, so on the
    // same layer it lands on top of the very menu it is meant to sit behind --
    // whether it receives the click at all comes down to surface stacking
    // order, which is why this behaved differently depending on which menu was
    // opened. A grab asks the compositor directly and does not care about
    // stacking. TrayMenu already worked this way.
    HyprlandFocusGrab {
        id: focusGrab
        active: menuWindow.shown
        windows: [menuWindow]
        onCleared: {
            if (menuWindow.shown && !menuWindow.dismissInhibited)
                menuWindow.dismissed();
        }
    }

    // Escape closes, from anywhere in the menu.
    //
    // A Shortcut rather than a focused Item with Keys.onEscapePressed: an item
    // that takes focus to listen for a key takes it away from the search
    // fields, password boxes and task inputs inside these menus, which would
    // break typing everywhere while looking perfectly fine in a test that only
    // clicks.
    Shortcut {
        sequences: ["Escape"]
        enabled: menuWindow.shown
        onActivated: menuWindow.dismissed()
    }

    // Registration lives in a Connections rather than an onVisibleChanged
    // handler on purpose: a subclass that declares its own onVisibleChanged
    // would shadow a handler defined here, and silently stop registering.
    // Both run when it is done this way.
    Connections {
        target: menuWindow
        enabled: menuWindow.managedByMenuService
        function onVisibleChanged() {
            if (menuWindow.visible)
                MenuService.register(menuWindow);
            else
                MenuService.unregister(menuWindow);
        }
    }

    Component.onDestruction: {
        if (menuWindow.managedByMenuService)
            MenuService.unregister(menuWindow);
    }
}
