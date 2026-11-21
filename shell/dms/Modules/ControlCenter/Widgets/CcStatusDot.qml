import QtQuick
import qs.dms.Common
import qs.dms.Modules.ControlCenter

Rectangle {
    property real size: CcMetrics.statusDotSize

    width: size
    height: size
    radius: size / 2
    color: Theme.primary
}
