import QtQuick
import Quickshell

import qs.end4.modules.common

LazyLoader {
    property bool extraCondition: true
    active: Config.ready && extraCondition
}
