pragma Singleton
import "."
import QtQuick

QtObject {
    // Feature Toggles
    property bool enableMedia: true
    property bool enableWeather: true
    property bool enableTodoList: true
    property bool enableResources: true
    property bool enablePowerProfiles: true
    
    // Quick Settings
    property int debounceInterval: 200
    property int hideTimerInterval: 250
}
