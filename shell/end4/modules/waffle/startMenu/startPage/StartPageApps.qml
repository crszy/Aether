pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.common.widgets
import qs.end4.modules.waffle.looks

BodyRectangle {
    id: root

    ColumnLayout {
        anchors {
            fill: parent
            leftMargin: 32
            rightMargin: 32
            topMargin: 25
            bottomMargin: 30
        }
        spacing: 26

        PinnedApps {
            Layout.fillWidth: true
        }

        AllApps {
            implicitHeight: 300 // for now
        }
    }

    component PinnedApps: PageSection {
        title: Translation.tr("Pinned")

        BigAppGrid {
            Layout.fillWidth: true
            columns: 8
            desktopEntries: Config.options.launcher.pinnedApps.map(appId => DesktopEntries.byId(appId))
        }
    }

    component AllApps: PageSection {
        title: Translation.tr("All")
        // TODO: Do we wanna also implement list view and grid view?
        //       (instead of only category view)
        AllAppsGrid {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 32
            Layout.rightMargin: 32
        }
    }

    component PageSection: ColumnLayout {
        id: pageSection
        required property string title
        default property alias pageData: pageSectionContentArea.data

        spacing: 16

        WText {
            Layout.leftMargin: 32
            text: pageSection.title
            font.pixelSize: Looks.font.pixelSize.large
            font.weight: Looks.font.weight.stronger
        }

        ColumnLayout {
            id: pageSectionContentArea
            Layout.fillWidth: true
        }
    }
}
