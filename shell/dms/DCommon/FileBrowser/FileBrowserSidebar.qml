pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

DFlickable {
    id: sidebar

    property var places: []
    property string currentPath: ""

    readonly property var rows: places.concat([
        {
            "key": "root",
            "name": "/",
            "path": "/",
            "iconName": "drive-harddisk"
        }
    ])

    signal placeSelected(string path)

    function labelFor(place) {
        switch (place.key) {
        case "home":
            return I18n.tr("Home", "file browser quick access location");
        case "root":
            return I18n.tr("File System", "file browser sidebar entry for the root directory");
        default:
            return place.name;
        }
    }

    clip: true
    contentHeight: section.implicitHeight + FileBrowserMetrics.sidebarPadding * 2

    FilePlaceSection {
        id: section

        x: FileBrowserMetrics.sidebarPadding
        y: FileBrowserMetrics.sidebarPadding
        width: sidebar.width - FileBrowserMetrics.sidebarPadding * 2
        title: I18n.tr("Quick Access", "file browser sidebar section header")

        Repeater {
            model: sidebar.rows

            FilePlaceRow {
                required property var modelData

                label: sidebar.labelFor(modelData)
                iconName: modelData.iconName
                path: modelData.path
                selected: sidebar.currentPath === modelData.path
                onClicked: sidebar.placeSelected(modelData.path)
            }
        }
    }
}
