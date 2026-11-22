import QtQuick
import qs.dms.Common
import qs.dms.Modules.Settings.Widgets
import qs.dms.Modules.SystemUpdate
import qs.dms.Services
import qs.dms.DCommon.Widgets
import qs.dms.Widgets

DFloatingWindow {
    id: root

    function show() {
        visible = true;
    }

    function hide() {
        visible = false;
    }

    objectName: "systemUpdateModal"
    title: I18n.tr("Software updates", "settings page, modal and bar widget title, DMS and system package updates")
    minimumSize: Qt.size(SettingsMetrics.windowMinWidth, SettingsMetrics.windowMinHeight)
    implicitWidth: SettingsMetrics.formDialogWidth + SettingsMetrics.pagePaddingH * 2
    readonly property real defaultHeight: SettingsMetrics.windowHeight - SettingsMetrics.basePagePaddingV * 4
    implicitHeight: heightWithin(defaultHeight, SettingsMetrics.pagePaddingH * 2 + Theme.spacingL)
    visible: false

    Ref {
        service: SystemUpdateService
        active: root.visible
    }

    onClosed: hide()

    Column {
        anchors.fill: parent
        spacing: 0

        DWindowHeader {
            id: titleBar
            width: parent.width
            controls: windowControls
            title: root.title
            onCloseRequested: root.hide()
        }

        SystemUpdatePanel {
            width: parent.width
            height: parent.height - titleBar.height
            hostVisible: root.visible
            onCloseRequested: root.hide()
        }
    }

    FloatingWindowControls {
        id: windowControls
        targetWindow: root
    }
}
