import QtQuick

QtObject {
    property string name
    property string ssid
    property int signalStrength
    property int security
    property bool connected
    property bool known
    function connect() {}
    function disconnect() {}
    function forget() {}
}
