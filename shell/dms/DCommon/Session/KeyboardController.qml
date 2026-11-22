pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common

Item {
    id: keyboard_controller
    readonly property var log: Log.scoped("KeyboardController")

    property Item target
    property bool isKeyboardActive: false
    property bool expressive: false

    property var rootObject

    function show() {
        if (keyboard?.closing) {
            keyboard.closing = false;
            keyboard.opacity = 1;
            isKeyboardActive = true;
            return;
        }
        if (isKeyboardActive || keyboard !== null) {
            log.debug("The keyboard is already shown");
            return;
        }
        keyboard = keyboardComponent.createObject(rootObject, {
            target: target,
            expressive: expressive
        });
        if (!keyboard)
            return;
        isKeyboardActive = true;
        keyboard.opacity = 1;
    }

    function hide() {
        if (!isKeyboardActive || keyboard === null) {
            log.debug("The keyboard is already hidden");
            return;
        }
        isKeyboardActive = false;
        if (expressive && LockMetrics.effectsDuration > 0) {
            keyboard.closing = true;
            keyboard.opacity = 0;
            return;
        }
        destroyKeyboard();
    }

    function destroyKeyboard() {
        if (!keyboard)
            return;
        keyboard.destroy();
        keyboard = null;
        isKeyboardActive = false;
    }

    Component.onDestruction: destroyKeyboard()

    property Item keyboard: null
    Component {
        id: keyboardComponent
        Keyboard {
            opacity: keyboard_controller.expressive ? 0 : 1
            onDismissed: keyboard_controller.hide()
            onClosed: keyboard_controller.destroyKeyboard()
        }
    }
}
