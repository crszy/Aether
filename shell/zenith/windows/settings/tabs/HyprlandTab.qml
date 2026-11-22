import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "../../../Settings"
import "../components"
import "../../../" as Shell

ColumnLayout {
    spacing: 0
    
    Text { text: "Hyprland Overrides (Real-time)"; font.pixelSize: 18; font.bold: true; color: "white"; Layout.margins: 20; Layout.topMargin: 20 }
    
    SettingRow {
        label: "Gaps In"
        NumberInput {
            from: 0; to: 50; step: 1
            value: HyprlandSettings.gapsIn
            onValueModified: HyprlandSettings.gapsIn = value
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }
    
    SettingRow {
        label: "Gaps Out"
        NumberInput {
            from: 0; to: 100; step: 2
            value: HyprlandSettings.gapsOut
            onValueModified: HyprlandSettings.gapsOut = value
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }

    SettingRow {
        label: "Rounding"
        NumberInput {
            from: 0; to: 30; step: 1
            value: HyprlandSettings.rounding
            onValueModified: HyprlandSettings.rounding = value
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }

    SettingRow {
        label: "Border Size"
        NumberInput {
            from: 0; to: 10; step: 1
            value: HyprlandSettings.borderSize
            onValueModified: HyprlandSettings.borderSize = value
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }

    SettingRow {
        label: "Shadow Range"
        NumberInput {
            from: 0; to: 100; step: 2
            value: HyprlandSettings.shadowRange
            onValueModified: HyprlandSettings.shadowRange = value
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }

    SettingRow {
        label: "Bar Margins"
        NumberInput {
            from: 0; to: 50; step: 1
            value: HyprlandSettings.barMargins
            onValueModified: HyprlandSettings.barMargins = value
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }

    Item { Layout.fillHeight: true }
}
