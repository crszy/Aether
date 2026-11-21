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
            text: "Bar Configuration"
            font.pixelSize: Shell.Theme.scaled(20)
            font.weight: Font.Bold
            color: Shell.Theme.text
        }
    }
    
    SettingRow {
        label: "Height"
        NumberInput {
            from: 20; to: 100; step: 2
            value: BarSettings.height
            onValueModified: BarSettings.height = value
        }
    }
    
    SettingRow {
        label: "Radius"
        NumberInput {
            from: 0; to: 50; step: 2
            value: BarSettings.radius
            onValueModified: BarSettings.radius = value
        }
    }

    SettingRow {
        label: "Opacity"
        MaterialSlider {
            from: 0; to: 1; stepSize: 0.05
            value: BarSettings.opacity
            onMoved: BarSettings.opacity = value
        }
    }

    Item { height: Shell.Theme.scaled(20) }

    SettingRow {
        label: "Entry Animation"
        MaterialSwitch {
            checked: BarSettings.entryAnimation
            onClicked: BarSettings.entryAnimation = checked
        }
    }
    
    SettingRow {
        label: "Duration (ms)"
        NumberInput {
            from: 100; to: 3000; step: 100
            value: BarSettings.animationDuration
            onValueModified: BarSettings.animationDuration = value
        }
    }

    Item { Layout.fillHeight: true }
}
