import QtQuick
import QtQuick.Window
import qs.dms.Common
import qs.dms.Modules.Settings
import qs.dms.Modules.Settings.Widgets
import qs.dms.DCommon.Widgets
import qs.dms.Widgets

DFloatingWindow {
    id: root

    signal ruleSubmitted

    objectName: "windowRuleModal"
    title: editor.title
    minimumSize: Qt.size(Math.min(SettingsMetrics.formDialogWidth + Theme.spacingL * 2, Screen.width), Math.min(Math.round(Theme.fontSizeMedium * 52), Screen.height))
    maximumSize: minimumSize
    implicitWidth: minimumSize.width
    implicitHeight: minimumSize.height
    visible: false

    onClosed: hide()

    function show(window) {
        visible = true;
        editor.show(window);
    }

    function showEdit(rule) {
        visible = true;
        editor.showEdit(rule);
    }

    function showCopy(rule) {
        visible = true;
        editor.showCopy(rule);
    }

    function hide() {
        editor.opened = false;
        visible = false;
    }

    WindowRuleEditorDialog {
        id: editor
        anchors.fill: parent
        windowControls: ruleWindowControls
        onRejected: root.hide()
        onRuleSubmitted: {
            root.ruleSubmitted();
            root.hide();
        }
    }

    FloatingWindowControls {
        id: ruleWindowControls
        targetWindow: root
    }
}
