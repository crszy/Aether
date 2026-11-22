// A desktop widget. Files starting with "_" are switched off - rename this to Clock.qml to see it.
// Where it sits: desktop.json in the folder above.
import QtQuick
import QtQuick.Layouts
import qs.components
import qs.services

ColumnLayout {
    spacing: 0

    StyledText {
        text: Qt.formatTime(Time.date, "hh:mm")
        color: Colours.palette.m3onSurface
        font.pointSize: 54
        font.weight: 300
    }

    StyledText {
        text: Qt.formatDate(Time.date, "dddd d MMMM")
        color: Colours.palette.m3onSurfaceVariant
        font.pointSize: 13
    }
}
