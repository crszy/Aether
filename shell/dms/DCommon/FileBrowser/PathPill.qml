pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

StyledButton {
    id: pill

    property string label: ""
    property string iconName: ""
    property string path: ""
    property bool current: false
    property bool flat: false
    property bool dropEnabled: false
    property color chipColor: Style.chipSurface

    readonly property bool dropTarget: dropZone.containsDrag
    readonly property color contentColor: dropTarget ? Style.onPrimaryContainer : flat ? (current ? Style.surfaceText : Style.surfaceTextSecondary) : current ? Style.onSelectedContainer : Style.surfaceText

    signal middleClicked
    signal menuRequested(real pointX, real pointY)
    signal dropped(var drop)
    signal springLoaded

    focusPolicy: Qt.TabFocus
    implicitWidth: row.implicitWidth + FileBrowserMetrics.pathPillPadding * 2
    implicitHeight: FileBrowserMetrics.pathPillHeight
    radius: FileBrowserMetrics.pathPillRadius
    color: dropTarget ? Style.primaryContainer : flat ? "transparent" : current ? Style.selectedContainer : chipColor
    Accessible.name: label

    StateLayer {
        control: pill
        stateColor: pill.contentColor
    }

    TapHandler {
        acceptedButtons: Qt.MiddleButton
        onTapped: pill.middleClicked()
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: point => pill.menuRequested(point.position.x, point.position.y)
    }

    Keys.onPressed: event => {
        const shifted = (event.modifiers & Qt.ShiftModifier) !== 0;
        if (event.key !== Qt.Key_Menu && !(event.key === Qt.Key_F10 && shifted))
            return;
        event.accepted = true;
        pill.menuRequested(pill.width / 2, pill.height / 2);
    }

    FileDropArea {
        id: dropZone

        anchors.fill: parent
        dropPath: pill.path
        enabled: pill.dropEnabled && pill.path !== ""
        springLoad: !pill.current
        onDropped: drop => pill.dropped(drop)
        onSpringLoaded: pill.springLoaded()
    }

    FocusRing {
        anchors.margins: 0
        radius: pill.radius
        visible: pill.visualFocus
    }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: FileBrowserMetrics.pathPillSpacing

        DIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.iconName !== ""
            name: pill.iconName
            size: FileBrowserMetrics.pathIconSize
            color: pill.flat || pill.current || pill.dropTarget ? pill.contentColor : Style.primary
        }

        StyledText {
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.label !== ""
            text: pill.label
            color: pill.contentColor
            font.pixelSize: Style.fontSizeMedium
            font.weight: Style.fontWeightMedium
        }
    }
}
