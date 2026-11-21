// DankMaterialShell's window header (DCommon/Widgets/DWindowHeader.qml) as an app's title bar: the title centred in
// DMS's title font, DMS's round minimise / maximise ("fullscreen") / close buttons at the right with DMS's spacing, the
// close one on secondaryContainer, DMS's hover / press state layer, all in DMS's own theme (DC.Style).
import QtQuick
import qs.modules.captions
import qs.dms.DCommon.Common as DC
import qs.dms.DCommon.Widgets as DW

Rectangle {
    id: root

    required property var skin

    readonly property CaptionTone tone: CaptionTone {
        skin: root.skin
        own: DC.Style.surfaceContainer
    }
    // DMS's light-scheme text / icon colours on a light app, its own (dark) ones otherwise
    readonly property color textColor: tone.light ? (skin.active ? "#1d1b20" : "#625f68") : (skin.active ? DC.Style.surfaceText : DC.Style.surfaceTextMedium)
    readonly property color iconColor: tone.light ? "#49454f" : DC.Style.onSurfaceVariant
    readonly property real buttonSize: Math.min(DC.Style.buttonHeightXS, height - 8)
    readonly property real inset: Math.min(DC.Style.windowInset, height / 3)

    color: tone.background
    topLeftRadius: skin.cornerRadius
    topRightRadius: skin.cornerRadius

    CaptionArea {
        skin: root.skin
    }

    // the app icon just before the (centred) title, when Settings asks for it
    CaptionIcon {
        skin: root.skin
        x: title.x + (title.width - title.contentWidth) / 2 - width - DC.Style.spacingS
        anchors.verticalCenter: parent.verticalCenter
        size: Math.min(16, root.height - 10)
    }

    // DWindowHeader: title centred between the edge inset and the buttons, ellipsed
    DW.StyledText {
        id: title

        readonly property real reserve: buttons.width + root.inset + DC.Style.spacingM

        x: reserve
        width: Math.max(0, root.width - 2 * reserve)
        anchors.verticalCenter: parent.verticalCenter
        text: root.skin.title
        font.pixelSize: Math.min(DC.Style.fontSizeLarge, root.height * 0.42)
        font.weight: DC.Style.fontWeightMedium
        color: root.textColor
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }

    component Button: CaptionButton {
        id: button

        property string icon
        property color base: "transparent"
        property color iconColor: root.iconColor

        width: root.buttonSize
        height: root.buttonSize

        Rectangle {
            anchors.fill: parent
            // DMS's buttons squircle a little when pressed (Style.buttonRadius)
            radius: DC.Style.buttonRadius(width, height, root.buttonSize, button.down, true)
            color: button.base

            Behavior on radius {
                NumberAnimation {
                    duration: DC.Style.expressiveDurations.expressiveEffects
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: DC.Style.expressiveCurves.standard
                }
            }

            // DMS's StateLayer: hover / pressed tint in the icon's colour
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: button.iconColor
                opacity: button.down ? DC.Style.stateLayerPressed : button.hovered ? DC.Style.stateLayerHover : 0

                Behavior on opacity {
                    NumberAnimation {
                        duration: DC.Style.shorterDuration
                    }
                }
            }

            DW.DIcon {
                anchors.centerIn: parent
                name: button.icon
                size: Math.min(DC.Style.iconSizeSmall, root.buttonSize * 0.6)
                color: button.iconColor
            }
        }
    }

    Row {
        id: buttons

        anchors.right: parent.right
        anchors.rightMargin: root.inset
        anchors.verticalCenter: parent.verticalCenter
        spacing: Math.min(DC.Style.windowInset, root.height / 4)

        Button {
            visible: root.skin.canMinimize
            icon: "minimize"
            onClicked: root.skin.minimize()
        }

        Button {
            visible: root.skin.canMaximize
            icon: root.skin.maximized ? "fullscreen_exit" : "fullscreen"
            onClicked: root.skin.toggleMaximize()
        }

        Button {
            icon: "close"
            base: DC.Style.secondaryContainer
            iconColor: DC.Style.onSecondaryContainer
            onClicked: root.skin.close()
        }
    }
}
