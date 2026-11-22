pragma ComponentBehavior: Bound

// RowButton with a shell's own logo (modules/nexus/assets/shells/<shell>.svg) where the icon goes. A shell that
// ships no logo (Zenith) gets its initial in the scheme's colours.
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Windows as Win
import Caelestia.Config
import qs.components
import qs.services

ConnectedRect {
    id: root

    property string shell
    property alias text: label.text
    property alias subtext: subLabel.text
    property string trailingIcon

    readonly property string logo: {
        const f = Quickshell.shellPath(`modules/nexus/assets/shells/${shell}.svg`);
        return Win.FsUtils.classify(f) === "file" ? `file:///${f}` : "";
    }

    signal clicked(event: MouseEvent)

    Layout.fillWidth: true
    implicitHeight: row.implicitHeight + Tokens.padding.medium * 2

    StateLayer {
        onClicked: e => root.clicked(e)
    }

    RowLayout {
        id: row

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Tokens.padding.largeIncreased
        spacing: Tokens.spacing.medium

        Item {
            implicitWidth: 32
            implicitHeight: 32

            // Caelestia's own logo component, in the scheme's colours
            Logo {
                visible: root.shell === "caelestia"
                anchors.centerIn: parent
                width: parent.width
                height: width * implicitHeight / implicitWidth
            }
            Image {
                anchors.fill: parent
                visible: root.logo !== "" && root.shell !== "caelestia"
                source: root.logo
                sourceSize.width: 64
                sourceSize.height: 64
                fillMode: Image.PreserveAspectFit
            }
            StyledRect {
                anchors.fill: parent
                visible: root.logo === ""
                radius: width / 2
                color: Colours.palette.m3secondaryContainer

                StyledText {
                    anchors.centerIn: parent
                    text: root.text.charAt(0)
                    color: Colours.palette.m3onSecondaryContainer
                    font: Tokens.font.body.medium
                }
            }
        }

        Column {
            Layout.fillWidth: true
            spacing: 0

            StyledText {
                id: label

                anchors.left: parent.left
                anchors.right: parent.right
                font: Tokens.font.body.small
                elide: Text.ElideRight
            }

            StyledText {
                id: subLabel

                anchors.left: parent.left
                anchors.right: parent.right
                visible: text
                color: Colours.palette.m3outline
                font: Tokens.font.label.small
                elide: Text.ElideRight
            }
        }

        MaterialIcon {
            visible: root.trailingIcon !== ""
            text: root.trailingIcon
            color: Colours.palette.m3onSurfaceVariant
            fontStyle: Tokens.font.icon.medium
        }
    }
}
