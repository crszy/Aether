import QtQuick
import qs.dms.DCommon.Common

Column {
    id: root

    property string confirmTitle: ""
    property string confirmMessage: ""
    property string confirmButtonText: I18n.tr("Confirm")
    property string cancelButtonText: I18n.tr("Cancel")
    property bool danger: false
    property string reviewUrl: ""
    property int selectedButton: 0
    property bool keyboardNavigation: false

    signal buttonActivated(int button)
    signal cancelled

    spacing: Style.spacingL
    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true
    Accessible.role: Accessible.Dialog
    Accessible.name: confirmTitle
    Accessible.description: confirmMessage

    onSelectedButtonChanged: {
        if (keyboardNavigation)
            focusSelection();
    }

    function focusSelection() {
        const button = selectedButton === 1 ? confirmButton : cancelButton;
        if (button.activeFocus && !button.visualFocus)
            button.focus = false;
        button.forceActiveFocus(Qt.TabFocusReason);
    }

    function reset() {
        keyboardNavigation = true;
        selectedButton = 0;
        focusSelection();
    }

    function handleKey(event) {
        switch (event.key) {
        case Qt.Key_Escape:
            cancelled();
            break;
        case Qt.Key_Left:
        case Qt.Key_Right:
            keyboardNavigation = true;
            selectedButton = event.key === (I18n.isRtl ? Qt.Key_Left : Qt.Key_Right) ? 1 : 0;
            focusSelection();
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (!event.isAutoRepeat)
                buttonActivated(selectedButton === 1 ? 1 : 0);
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    StyledText {
        width: parent.width
        text: root.confirmTitle
        textFormat: Text.PlainText
        font.pixelSize: Style.fontSizeLarge
        font.weight: Style.fontWeightMedium
        color: Style.onSurface
        wrapMode: Text.Wrap
    }

    StyledText {
        width: parent.width
        text: root.confirmMessage
        textFormat: Text.PlainText
        font.pixelSize: Style.fontSizeMedium
        color: Style.onSurfaceVariant
        wrapMode: Text.Wrap
    }

    DButton {
        id: reviewButton
        KeyNavigation.tab: cancelButton
        KeyNavigation.backtab: confirmButton
        visible: root.reviewUrl !== ""
        text: I18n.tr("Review changes", "open plugin changes before updating")
        iconName: "open_in_new"
        maximumWidth: root.width
        wrapText: true
        variant: "tonal"
        onClicked: Qt.openUrlExternally(root.reviewUrl)
    }

    Row {
        width: parent.width
        spacing: Style.spacingS

        DButton {
            id: cancelButton
            KeyNavigation.tab: confirmButton
            KeyNavigation.backtab: reviewButton.visible ? reviewButton : confirmButton
            width: (parent.width - parent.spacing) / 2
            maximumWidth: width
            wrapText: true
            text: root.cancelButtonText
            variant: "tonal"
            onActiveFocusChanged: {
                if (activeFocus)
                    root.selectedButton = 0;
            }
            onClicked: root.buttonActivated(0)
        }

        DButton {
            id: confirmButton
            KeyNavigation.tab: reviewButton.visible ? reviewButton : cancelButton
            KeyNavigation.backtab: cancelButton
            width: (parent.width - parent.spacing) / 2
            maximumWidth: width
            wrapText: true
            text: root.confirmButtonText
            variant: root.danger ? "tonal" : "filled"
            danger: root.danger
            onActiveFocusChanged: {
                if (activeFocus)
                    root.selectedButton = 1;
            }
            onClicked: root.buttonActivated(1)
        }
    }
}
