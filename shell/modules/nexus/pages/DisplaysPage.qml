pragma ComponentBehavior: Bound

// Settings -> Displays: every monitor, turn it on or off, its Windows scale (100 %, 125 % ...), main display.
// Changes go through Quickshell.Windows.Displays, which undoes them by itself unless you press "Keep" in time,
// so a monitor you can no longer see comes back on its own.
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Windows as Win
import Caelestia.Config
import Caelestia.I18n
import qs.components
import qs.services
import qs.modules.nexus.common

PageBase {
    id: root

    title: Tr.tr("Displays")

    Component.onCompleted: Win.Displays.refresh()

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: root.cappedWidth
        spacing: Tokens.spacing.extraSmall / 2

        // ---- keep / undo after a change ----
        InfoRow {
            visible: Win.Displays.pendingConfirmation
            first: true
            icon: "timer"
            label: Tr.tr("Keep these display settings?")
            subtext: Tr.tr("They are undone by themselves in %1 s").arg(Win.Displays.confirmSecondsLeft)
        }

        RowButton {
            visible: Win.Displays.pendingConfirmation
            icon: "check"
            text: Tr.tr("Keep")
            onClicked: Win.Displays.keepChanges()
        }

        RowButton {
            visible: Win.Displays.pendingConfirmation
            last: true
            icon: "undo"
            text: Tr.tr("Undo now")
            onClicked: Win.Displays.revertChanges()
        }

        InfoRow {
            visible: Win.Displays.lastError.length > 0
            first: true
            last: true
            icon: "error"
            iconColour: Colours.palette.m3error
            label: Tr.tr("The last change did not work")
            subtext: Win.Displays.lastError
        }

        Repeater {
            model: Win.Displays.displays

            ColumnLayout {
                id: disp

                required property var modelData

                Layout.fillWidth: true
                spacing: Tokens.spacing.extraSmall / 2

                SectionHeader {
                    text: `${disp.modelData.number}. ${disp.modelData.name || disp.modelData.gdiName}${disp.modelData.primary ? "  ·  " + Tr.tr("main display") : ""}`
                }

                ToggleRow {
                    first: true
                    last: !disp.modelData.active
                    text: Tr.tr("On")
                    subtext: disp.modelData.active
                        ? Tr.tr("Off: Windows stops using this monitor until you turn it back on here")
                        : Tr.tr("This monitor is off")
                    checked: disp.modelData.active
                    onToggled: Win.Displays.setEnabled(disp.modelData.id, checked)
                }

                InfoRow {
                    visible: disp.modelData.active
                    icon: "aspect_ratio"
                    label: Tr.tr("Resolution")
                    value: `${disp.modelData.resolutionWidth} × ${disp.modelData.resolutionHeight}  ·  ${disp.modelData.refreshRate} Hz`
                }

                StepperRow {
                    visible: disp.modelData.active && disp.modelData.scaleSupported
                    label: Tr.tr("Scale")
                    subtext: Tr.tr("Makes text and apps on this monitor bigger or smaller (Windows recommends %1 %)").arg(disp.modelData.scaleRecommended)
                    value: disp.modelData.scale
                    from: Math.min(...(disp.modelData.scales.length ? disp.modelData.scales : [100]))
                    to: Math.max(...(disp.modelData.scales.length ? disp.modelData.scales : [100]))
                    stepSize: 25
                    onMoved: v => Win.Displays.setScale(disp.modelData.id, v)
                }

                RowButton {
                    visible: disp.modelData.active && !disp.modelData.primary
                    last: true
                    icon: "star"
                    text: Tr.tr("Make this the main display")
                    onClicked: Win.Displays.setPrimary(disp.modelData.id)
                }

                Item {
                    visible: disp.modelData.active && disp.modelData.primary
                    Layout.preferredHeight: 0
                }
            }
        }
    }
}
