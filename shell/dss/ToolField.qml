// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// A text field of the toolbox: a soft pill that gets an outline in your key color and squarer corners when you
// type in it. accepted(text) on Enter.
Rectangle {
    id: f
    property var pal
    property string hint: ""
    property string icon: ""
    property string glyph: ""
    property alias text: input.text
    property alias input: input
    property bool secret: false
    property bool numbers: false
    signal accepted(string text)
    signal edited(string text)
    implicitHeight: 46
    height: implicitHeight
    radius: input.activeFocus ? 16 : 23
    Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    color: input.activeFocus ? pal.cardHi : pal.card
    Behavior on color { ColorAnimation { duration: 180 } }
    border.width: 2; border.color: input.activeFocus ? pal.primary : "transparent"
    Behavior on border.color { ColorAnimation { duration: 180 } }
    MIcon { id: ic; visible: f.icon !== ""; x: 14; anchors.verticalCenter: parent.verticalCenter; size: 18; name: f.icon; glyph: f.glyph
        color: input.activeFocus ? f.pal.primary : f.pal.dim
        Behavior on color { ColorAnimation { duration: 180 } } }
    TextInput {
        id: input
        x: f.icon !== "" ? 42 : 18
        width: parent.width - x - 16
        anchors.verticalCenter: parent.verticalCenter
        color: f.pal.fg; selectionColor: f.pal.primary; selectedTextColor: f.pal.inkPrimary
        font.family: f.numbers ? Fonts.numbers : Fonts.main; font.pixelSize: 14
        clip: true; selectByMouse: true
        echoMode: f.secret ? TextInput.Password : TextInput.Normal
        onAccepted: f.accepted(text)
        onTextEdited: f.edited(text)
        cursorDelegate: Rectangle { width: 2; radius: 1; color: f.pal.primary; visible: input.activeFocus
            SequentialAnimation on opacity { loops: Animation.Infinite; running: input.activeFocus
                NumberAnimation { to: 1; duration: 80 } PauseAnimation { duration: 480 }
                NumberAnimation { to: 0.15; duration: 260 } PauseAnimation { duration: 180 } } }
        Text { text: f.hint; color: f.pal.dim; font: input.font
            opacity: input.text === "" ? 1 : 0; x: input.text === "" ? 0 : 10
            Behavior on opacity { NumberAnimation { duration: 150 } }
            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } } }
    }
}
