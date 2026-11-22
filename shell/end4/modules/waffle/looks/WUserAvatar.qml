pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.common.widgets
import qs.end4.modules.waffle.looks

StyledImage {
    id: avatar
    Layout.alignment: Qt.AlignTop
    sourceSize: Qt.size(32, 32)
    source: Directories.userAvatarPathAccountsService
    fallbacks: [Directories.userAvatarPathRicersAndWeirdSystems, Directories.userAvatarPathRicersAndWeirdSystems2]

    layer.enabled: true
    layer.effect: OpacityMask {
        maskSource: Circle {
            diameter: avatar.height
        }
    }
}
