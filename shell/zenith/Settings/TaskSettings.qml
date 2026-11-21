pragma Singleton
import QtQuick

QtObject {
    // Configurable intervals for recurring & repeating tasks (ms)
    property int fastPollInterval: 1500       // Active menu telemetry & bandwidth
    property int mediumPollInterval: 4000     // Active menu refreshes, MPRIS updates
    property int slowPollInterval: 15000      // Low-power background status when popups closed
    property int lazyPollInterval: 60000      // System updates, battery check
    property int idlePollInterval: 600000     // Weather, calendar sync (10 mins)
    
    // Dynamic adaptive polling rate toggle
    property bool adaptivePollingEnabled: true
}
