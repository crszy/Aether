// A clock for Caelestia's lock screen (just the clock slot). Use it: windows.json "lockClock": "user:BigClock"
import QtQuick
import qs.components
import qs.services

StyledText {
    text: Qt.formatTime(Time.date, "hh:mm")
    color: Colours.palette.m3primary
    font.pointSize: 64
    font.weight: 700
}
