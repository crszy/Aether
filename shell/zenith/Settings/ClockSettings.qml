import QtQuick
import Quickshell
pragma Singleton
import "."

QtObject {
    // enable / disable
    property bool showClock: true
    property bool showDate: true
    // time format
    property bool use24Hour: false
    // formats
    property string dateFormat: "ddd dd"
    property string timeFormat12h: "hh:mm AP"
    property string timeFormat24h: "hh:mm"
    // precision
    property int precision: SystemClock.Minutes
}
