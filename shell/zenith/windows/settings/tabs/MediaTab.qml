import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "../../../Settings"
import "../components"
import "../../../" as Shell

ColumnLayout {
    spacing: 0
    
    Text { text: "Media Behavior"; font.pixelSize: 18; font.bold: true; color: "white"; Layout.margins: 20; Layout.topMargin: 20 }
    
    SettingRow {
        label: "Truncate Track Title"
        MaterialSwitch {
            checked: MediaSettings.truncateTrackTitle
            onClicked: MediaSettings.truncateTrackTitle = checked
        }
    }
    
    SettingRow {
        label: "Max Title Length"
        NumberInput {
            from: 10; to: 200; step: 5
            value: MediaSettings.maxTrackTitleLength
            onValueModified: MediaSettings.maxTrackTitleLength = value
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }

    SettingRow {
        label: "Auto Focus Player"
        MaterialSwitch {
            checked: MediaSettings.autoManageMediaFocus
            onClicked: MediaSettings.autoManageMediaFocus = checked
        }
    }

    SettingRow {
        label: "Bar Margins"
        NumberInput {
            from: 0; to: 50; step: 1
            value: MediaSettings.barMargins
            onValueModified: MediaSettings.barMargins = value
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }

    Item { Layout.fillHeight: true }
}
