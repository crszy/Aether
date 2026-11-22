import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "../../../Settings"
import "../components"
import "../../../" as Shell

ColumnLayout {
    spacing: 0
    
    Text { text: "Workspace Bar"; font.pixelSize: 18; font.bold: true; color: "white"; Layout.margins: 20; Layout.topMargin: 20 }
    
    SettingRow {
        label: "Background Style"
        SelectionBox {
            model: ["pills", "full"]
            currentIndex: model.indexOf(WorkspaceSettings.backgroundStyle)
            onActivated: WorkspaceSettings.backgroundStyle = model[index]
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }
    
    SettingRow {
        label: "Display Style"
        SelectionBox {
            model: ["numbers", "dots"]
            currentIndex: model.indexOf(WorkspaceSettings.displayStyle)
            onActivated: WorkspaceSettings.displayStyle = model[index]
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }

    SettingRow {
        label: "Active Width"
        NumberInput {
            from: 10; to: 100; step: 2
            value: WorkspaceSettings.activeWidth
            onValueModified: WorkspaceSettings.activeWidth = value
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }

    SettingRow {
        label: "Spacing"
        NumberInput {
            from: 0; to: 20; step: 1
            value: WorkspaceSettings.spacing
            onValueModified: WorkspaceSettings.spacing = value
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }

    SettingRow {
        label: "Bar Margins"
        NumberInput {
            from: 0; to: 50; step: 1
            value: WorkspaceSettings.barMargins
            onValueModified: WorkspaceSettings.barMargins = value
            Layout.preferredWidth: Shell.Theme.scaled(150)
        }
    }

    Item { Layout.fillHeight: true }
}
