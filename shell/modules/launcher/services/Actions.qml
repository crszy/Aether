pragma Singleton

import ".."
import QtQuick
import Quickshell
import Caelestia.Config
import Caelestia.I18n
import Caelestia.Services
import qs.services
import qs.utils
import qs.modules.rice

Searcher {
    id: root

    function transformSearch(search: string): string {
        return search.slice(GlobalConfig.launcher.actionPrefix.length);
    }

    list: [aetherAction, ...userVariants.instances, ...variants.instances, borderAction]
    useFuzzy: GlobalConfig.launcher.useFuzzy.actions

    Variants {
        id: variants

        model: {
            const enableDangerous = GlobalConfig.launcher.enableDangerousActions;
            return GlobalConfig.launcher.actions.values.filter(a => a.enabled && (enableDangerous || !a.dangerous));
        }

        Action {}
    }

    // Aether: >border (window border colour of the Hyprland window manager); always offered, not part of the saved config
    Action {
        id: borderAction

        modelData: ({ name: "Border", description: "Change the window border / highlight colour", icon: "border_style", command: ["autocomplete", "border"] })
    }

    // your own commands from ~/.config/caelestia/user/commands.json (UserExt): ">profile" ...
    Variants {
        id: userVariants

        model: UserExt.commands.map(c => ({ name: c.name, description: c.description ?? "", icon: c.icon ?? "bolt", command: ["user-command", c.name] }))

        Action {}
    }

    // Aether: >aether opens Aether Settings (every shell in one window); always offered
    Action {
        id: aetherAction

        modelData: ({ name: "Aether", description: "Aether Settings - every shell, rices and pieces", icon: "tune", command: ["aether-settings"] })
    }

    component Action: QtObject {
        required property var modelData
        readonly property string name: modelData.name ? Tr.trMarked(modelData.name) : Tr.trCtx("Unnamed", "launcher action with no name")
        readonly property string desc: modelData.description ? Tr.trMarked(modelData.description) : Tr.trCtx("No description", "launcher action with no description")
        readonly property string icon: modelData.icon ?? "help_outline"
        readonly property list<string> command: modelData.command ?? []
        readonly property bool enabled: modelData.enabled ?? true
        readonly property bool dangerous: modelData.dangerous ?? false

        function onClicked(list: AppList): void {
            if (command.length === 0)
                return;

            if (command[0] === "autocomplete" && command.length > 1) {
                list.search.text = `${GlobalConfig.launcher.actionPrefix}${command[1]} `;
            } else if (command[0] === "user-command" && command.length > 1) {
                list.screenState.launcher = false;
                UserExt.run(command[1]);
            } else if (command[0] === "aether-settings") {
                list.screenState.launcher = false;
                WinSettings.ipc("aether", "settings", []); // the new Aether Settings center, same as every other shell
            } else if (command[0] === "setMode" && command.length > 1) {
                list.screenState.launcher = false;
                Colours.setMode(command[1]);
            } else {
                list.screenState.launcher = false;
                if (!SessionManager.exec(command))
                    Quickshell.execDetached(command);
            }
        }
    }
}
