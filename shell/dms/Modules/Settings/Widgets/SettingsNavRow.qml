import QtQuick
import qs.dms.Common

SettingsRow {
    id: root

    property string hint: ""

    subtitle: hint
    resetKeys: []
    clickable: true
    showChevron: true
}
