import QtQuick
import qs.dms.Common
import qs.dms.Services
import qs.dms.Widgets

DOSD {
    id: root

    osdKind: "idleInhibitor"

    osdWidth: Theme.osdHeight
    osdHeight: Theme.osdHeight
    autoHideInterval: 2000
    enableMouseInteraction: false

    Connections {
        target: SessionService
        function onInhibitorChanged() {
            if (SettingsData.osdIdleInhibitorEnabled) {
                root.show();
            }
        }
    }

    content: Item {
        OsdIcon {
            tonal: false
            anchors.centerIn: parent
            iconName: "local_cafe"
            iconColor: SessionService.idleInhibited ? Theme.primary : Theme.outline
            filled: SessionService.idleInhibited
        }
    }
}
