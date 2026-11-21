// Colours for a title bar that blends with its app (Settings > Mix & match > "Blend title bars with the app", on by
// default): the header takes the colour along the top of the app's content, and `light` says whether that is light, so
// a style picks its light or dark text and buttons. Off, or before the colour is known, the style's own colour is used.
import QtQuick
import qs.services

QtObject {
    id: root

    required property var skin
    required property color own // the style's own header colour

    readonly property bool blend: WinSettings.titleBarBlend && skin.appColor.valid
    readonly property color background: blend ? skin.appColor : own
    readonly property bool light: (0.299 * background.r + 0.587 * background.g + 0.114 * background.b) > 0.55
}
