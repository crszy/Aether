import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets
import qs.dms.Modules.Plugins

PluginSettings {
    id: root
    pluginId: "wallpaperWatcherDaemon"

    StyledText {
        text: "Wallpaper Change Hook"
        font.pixelSize: Theme.fontSizeLarge
        font.weight: Theme.fontWeightMedium
        color: Theme.surfaceText
    }

    StringSetting {
        settingKey: "scriptPath"
        label: "Script Path"
        description: "Path to a script that will be executed when the wallpaper changes. The new wallpaper path will be passed as the first argument."
        placeholder: "/path/to/your/script.sh"
        defaultValue: ""
    }
}
