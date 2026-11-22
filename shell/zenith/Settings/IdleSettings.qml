pragma Singleton
import QtQuick

// Idle behaviour, in seconds. 0 disables a step. The order is the usual one:
// lock first, then screen off, then (optionally) suspend.
QtObject {
    property int lockTimeout: 300
    property int screenOffTimeout: 420
    property int suspendTimeout: 0

    // Do not go idle while something is playing / while on mains power.
    property bool inhibitWhenAudio: true
    property bool inhibitWhenCharging: false

    // Lock screen look
    property bool blurBackground: true
    property string profilePicture: ""   // empty: ~/.face, then the repo's profilePicture
}
