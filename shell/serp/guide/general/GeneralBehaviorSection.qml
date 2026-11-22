// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
import QtQuick
import "../../"
import "../../reusables"
import "../../reusables/guide"

SettingsBlob {
    id: section
    required property var tab
    readonly property var rootObj: tab.rootObj

    SettingsRow {
        rootObj: section.tab.rootObj
        icon: "󱓞"
        title: I18n.t("guide.general.quickactions.title") || "Quickactions"
        description: I18n.t("guide.general.quickactions.desc") || "Enable floating quickactions overlay"

        Toggle {
            checked: section.tab.quickactions
            accentColor: ThemeBackend.mauve
            baseColor: ThemeBackend.surface1
            handleColor: ThemeBackend.crust
            handleOffColor: ThemeBackend.text
            onToggled: function(c) {
                section.tab.quickactions = c;
                section.tab.updateGeneralSettings();
            }
        }
    }
}
