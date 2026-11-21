import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.waffle.looks

AppButton {
    id: root

    iconName: checked ? "system-search-checked" : "system-search"
    separateLightDark: true

    checked: GlobalStates.searchOpen && LauncherSearch.query !== ""
    onClicked: {
        GlobalStates.searchOpen = !GlobalStates.searchOpen; // For now...
    }

    BarToolTip {
        id: tooltip
        text: Translation.tr("Search")
        extraVisibleCondition: root.shouldShowTooltip
    }
}
