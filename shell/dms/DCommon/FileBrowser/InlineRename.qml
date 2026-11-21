pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

DTextField {
    id: field

    required property string name
    property bool isDir: false

    readonly property string trimmed: text.trim()
    readonly property bool valid: FileFormat.validName(text)

    signal committed(string name)
    signal cancelled

    function begin() {
        text = name;
        forceActiveFocus();
        select(0, FileFormat.stemLength(name, isDir));
    }

    function commit() {
        if (!valid) {
            invalidHint.showNow();
            return;
        }
        if (trimmed === name) {
            field.cancelled();
            return;
        }
        field.committed(trimmed);
    }

    height: Style.buttonHeightXXS
    topPadding: 0
    bottomPadding: 0
    outlined: true
    isError: text !== "" && !valid
    onAccepted: commit()
    Keys.onEscapePressed: event => {
        event.accepted = true;
        field.cancelled();
    }
    Keys.onReturnPressed: event => event.accepted = true
    Keys.onEnterPressed: event => event.accepted = true

    DTooltipHost {
        id: invalidHint

        target: field
        side: "bottom"
        text: I18n.tr("A name cannot be empty or contain a slash", "inline rename validation message")
    }
}
