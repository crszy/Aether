import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "../../../Settings"
import "../components"
import "../../../" as Shell

ColumnLayout {
    spacing: 0
    
    // Header
    Rectangle {
        Layout.fillWidth: true
        height: Shell.Theme.scaled(60)
        color: "transparent"
        Text {
            anchors.left: parent.left
            anchors.leftMargin: Shell.Theme.scaled(20)
            anchors.verticalCenter: parent.verticalCenter
            text: "General"
            font.pixelSize: Shell.Theme.scaled(20)
            font.weight: Font.Bold
            color: Shell.Theme.text
        }
    }
    
    SettingRow {
        label: "Enable Media"
        MaterialSwitch {
            checked: WidgetSettings.enableMedia
            onClicked: WidgetSettings.enableMedia = checked
        }
    }
    
    SettingRow {
        label: "Enable Battery"
        MaterialSwitch {
            checked: WidgetSettings.enableBattery
            onClicked: WidgetSettings.enableBattery = checked
        }
    }
    
    SettingRow {
        label: "Enable Resources"
        MaterialSwitch {
            checked: WidgetSettings.enableResources
            onClicked: WidgetSettings.enableResources = checked
        }
    }
    
    SettingRow {
        label: "Enable Power Profiles"
        MaterialSwitch {
            checked: WidgetSettings.enablePowerProfiles
            onClicked: WidgetSettings.enablePowerProfiles = checked
        }
    }

    Item { Layout.fillHeight: true }
}
