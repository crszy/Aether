import QtQuick
import QtQuick.Layouts
import Quickshell
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.waffle.looks
import qs.end4.modules.waffle.actionCenter

ActionCenterToggle {
    id: root

    name: Network.ethernet ? Translation.tr("Network") : Network.networkName


}
