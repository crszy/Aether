pragma Singleton
import Quickshell
import qs.end4.services
import qs.end4.modules.common

// Windows version of end-4's session actions (upstream: loginctl / systemctl / pkill Hyprland).
// Windows closes apps itself on shut down, so closeAllWindows does nothing.
Singleton {
    id: root

    function closeAllWindows() {}

    function changePassword() {
        WinActions.open(Config.options.apps.changePassword);
    }

    function lock() {
        WinActions.lock();
    }

    function suspend() {
        WinActions.suspend();
    }

    function logout() {
        WinActions.logout();
    }

    function launchTaskManager() {
        WinActions.open(Config.options.apps.taskManager);
    }

    function hibernate() {
        WinActions.hibernate();
    }

    function poweroff() {
        WinActions.poweroff();
    }

    function reboot() {
        WinActions.reboot();
    }

    function rebootToFirmware() {
        WinActions.rebootToFirmware();
    }
}
