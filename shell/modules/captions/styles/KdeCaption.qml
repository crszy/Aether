// KDE Plasma's Breeze window decoration (Breeze Dark) as an app's title bar: the app icon at the left (Breeze's
// window-menu button: click it for the window menu), the title centred in the regular UI font, and Breeze's line glyphs
// at the right: minimise (down chevron), maximise (up chevron, a diamond when maximised) and close (X). Hovering a
// button fills a circle behind its glyph, red for close.
import QtQuick
import QtQuick.Shapes
import qs.modules.captions

Rectangle {
    id: root

    required property var skin

    // Breeze Dark [WM] colours
    readonly property color activeBg: "#272c31"
    readonly property color inactiveBg: "#202428"
    readonly property color activeFg: "#fcfcfc"
    readonly property color inactiveFg: "#a1a9b1"
    readonly property color closeRed: "#da4453"
    readonly property CaptionTone tone: CaptionTone {
        skin: root.skin
        own: root.skin.active ? root.activeBg : root.inactiveBg
    }
    // Breeze Dark's text, or Breeze Light's on a light app
    readonly property color fg: tone.light ? (skin.active ? "#232627" : "#707d8a") : (skin.active ? activeFg : inactiveFg)
    readonly property real buttonSize: Math.min(18, height - 6)

    color: tone.background
    topLeftRadius: skin.cornerRadius
    topRightRadius: skin.cornerRadius

    Behavior on color {
        ColorAnimation {
            duration: 150
        }
    }

    CaptionArea {
        skin: root.skin
    }

    // Breeze's window-menu button: the app icon
    CaptionButton {
        id: menuButton

        x: 6
        anchors.verticalCenter: parent.verticalCenter
        width: icon.width
        height: icon.height
        visible: icon.visible
        onClicked: root.skin.showMenu()

        CaptionIcon {
            id: icon

            skin: root.skin
            byDefault: true
        }
    }

    Text {
        readonly property real reserve: Math.max(menuButton.x + menuButton.width + 6, buttons.width + 12)

        x: reserve
        width: Math.max(0, root.width - 2 * reserve)
        anchors.verticalCenter: parent.verticalCenter
        text: root.skin.title
        color: root.fg
        font.pixelSize: Math.round(Math.min(13, root.height * 0.45))
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }

    component Button: CaptionButton {
        id: button

        property bool isClose: false
        readonly property color glyph: hovered ? (isClose ? "white" : root.color) : root.fg

        width: root.buttonSize
        height: root.buttonSize

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: button.isClose ? root.closeRed : root.fg
            opacity: button.hovered ? (button.down ? 0.75 : 1) : 0

            Behavior on opacity {
                NumberAnimation {
                    duration: 120
                }
            }
        }
    }

    component Glyph: Shape {
        id: shape

        required property color stroke
        property alias path: poly.path

        anchors.centerIn: parent
        width: Math.round(root.buttonSize * 0.5)
        height: width
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: shape.stroke
            strokeWidth: 1.5
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathPolyline {
                id: poly
            }
        }
    }

    Row {
        id: buttons

        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        Button {
            id: minButton

            visible: root.skin.canMinimize
            onClicked: root.skin.minimize()

            Glyph {
                readonly property real s: width
                stroke: minButton.glyph
                path: [Qt.point(0, s * 0.3), Qt.point(s / 2, s * 0.75), Qt.point(s, s * 0.3)]
            }
        }

        Button {
            id: maxButton

            visible: root.skin.canMaximize
            onClicked: root.skin.toggleMaximize()

            Glyph {
                readonly property real s: width
                stroke: maxButton.glyph
                path: root.skin.maximized
                    ? [Qt.point(s / 2, 0), Qt.point(s, s / 2), Qt.point(s / 2, s), Qt.point(0, s / 2), Qt.point(s / 2, 0)]
                    : [Qt.point(0, s * 0.7), Qt.point(s / 2, s * 0.25), Qt.point(s, s * 0.7)]
            }
        }

        Button {
            id: closeButton

            isClose: true
            onClicked: root.skin.close()

            Glyph {
                readonly property real s: width
                stroke: closeButton.glyph
                path: [Qt.point(0, 0), Qt.point(s, s)]
            }
            Glyph {
                readonly property real s: width
                stroke: closeButton.glyph
                path: [Qt.point(s, 0), Qt.point(0, s)]
            }
        }
    }
}
