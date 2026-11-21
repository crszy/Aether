pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.waffle.looks

BodyRectangle {
    id: root

    property alias context: searchResults.context
    property string searchText: LauncherSearch.query
    property alias currentIndex: searchResults.currentIndex

    ColumnLayout {
        anchors {
            fill: parent
            topMargin: 2
            leftMargin: 24
            rightMargin: 24
        }
        spacing: 12

        TagStrip {
            context: root.context
            Layout.fillWidth: true
            Layout.fillHeight: false
        }

        SearchResults {
            id: searchResults
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
