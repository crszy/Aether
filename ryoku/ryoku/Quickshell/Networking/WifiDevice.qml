import QtQuick

QtObject {
    property string name
    property int type: 2
    property bool connected
    property bool scannerEnabled
    readonly property QtObject networks: QtObject { property var values: [] }
    function disconnect() {}
}
