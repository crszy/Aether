pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common

Item {
    id: root

    property date displayDate: new Date()
    property date selectedDate: new Date()
    property date today: new Date()
    property int firstDayOfWeek: 1
    property var dayNames: []
    property bool showWeekNumbers: false
    property var weekNumberFor: null
    property var dotColorsFor: null
    property int maxDots: 3
    property int revision: 0
    property bool interactive: true
    property real cellGap: Style.spacingXS
    property real weekdayRowHeight: Style.iconSizeMedium
    property real weekColumnWidth: Style.iconSizeLarge
    property real cellRadius: Style.cornerRadiusS
    property bool highlightWeekends: false
    property color weekendColor: Style.tertiary

    signal dayClicked(date date)

    readonly property int columns: 7
    readonly property int rows: 6
    readonly property bool floatingWindow: Style.isFloatingWindow(root)
    readonly property int firstFocusIndex: {
        for (let i = 0; i < columns * rows; i++) {
            if (sameDay(dateAt(i), selectedDate))
                return i;
        }
        return 0;
    }
    readonly property date firstDay: {
        const first = new Date(displayDate.getFullYear(), displayDate.getMonth(), 1, 12);
        const diff = (first.getDay() - firstDayOfWeek + columns) % columns;
        first.setDate(first.getDate() - diff);
        return first;
    }
    readonly property var weekdayLabels: {
        if (dayNames.length === columns)
            return dayNames;
        const names = [];
        for (let i = 0; i < columns; i++) {
            const qtDay = ((firstDayOfWeek + i + columns - 1) % columns) + 1;
            names.push(Qt.locale().dayName(qtDay, Locale.ShortFormat));
        }
        return names;
    }
    readonly property real gridLeft: showWeekNumbers ? weekColumnWidth + cellGap : 0
    readonly property real cellWidth: (width - gridLeft - cellGap * (columns - 1)) / columns
    readonly property real cellHeight: Math.max(0, (height - weekdayRowHeight - cellGap * rows) / rows)

    function dateAt(index) {
        const date = new Date(firstDay);
        date.setDate(date.getDate() + index);
        return date;
    }

    function isWeekend(date) {
        const qtDay = ((date.getDay() + 6) % 7) + 1;
        return !Qt.locale().weekDays.includes(qtDay);
    }

    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }

    Repeater {
        model: root.showWeekNumbers ? root.rows : 0

        StyledText {
            required property int index

            x: I18n.isRtl ? root.width - width : 0
            y: root.weekdayRowHeight + root.cellGap + index * (root.cellHeight + root.cellGap)
            width: root.weekColumnWidth
            height: root.cellHeight
            text: root.weekNumberFor ? root.weekNumberFor(root.dateAt(index * root.columns)) : ""
            font.pixelSize: Style.fontSizeSmall
            font.weight: Style.fontWeightMedium
            color: Style.onSurfaceVariant
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Repeater {
        model: root.columns

        StyledText {
            required property int index

            x: I18n.isRtl ? root.width - root.gridLeft - index * (root.cellWidth + root.cellGap) - width : root.gridLeft + index * (root.cellWidth + root.cellGap)
            y: 0
            width: root.cellWidth
            height: root.weekdayRowHeight
            text: root.weekdayLabels[index] ?? ""
            font.pixelSize: Style.fontSizeSmall
            font.weight: Style.fontWeightMedium
            color: root.highlightWeekends && root.isWeekend(root.dateAt(index)) ? root.weekendColor : Style.onSurfaceVariant
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Repeater {
        id: dayRepeater
        model: root.columns * root.rows

        StyledButton {
            id: cell

            required property int index

            readonly property date dayDate: root.dateAt(index)
            Accessible.role: Accessible.Button
            Accessible.name: dayDate.toLocaleDateString(Qt.locale(), Locale.LongFormat)
            Accessible.selected: isSelected
            enabled: root.interactive
            focusPolicy: activeFocus || index === root.firstFocusIndex ? Qt.StrongFocus : Qt.ClickFocus
            onClicked: root.dayClicked(dayDate)

            Keys.onPressed: event => {
                let offset = 0;
                switch (event.key) {
                case Qt.Key_Left:
                    offset = mirrored ? 1 : -1;
                    break;
                case Qt.Key_Right:
                    offset = mirrored ? -1 : 1;
                    break;
                case Qt.Key_Up:
                    offset = -root.columns;
                    break;
                case Qt.Key_Down:
                    offset = root.columns;
                    break;
                default:
                    return;
                }
                const next = dayRepeater.itemAt(index + offset);
                if (next)
                    next.forceActiveFocus(Qt.TabFocusReason);
                event.accepted = true;
            }

            FocusRing {
                visible: cell.visualFocus
            }
            readonly property bool inMonth: dayDate.getMonth() === root.displayDate.getMonth()
            readonly property bool isToday: root.sameDay(dayDate, root.today)
            readonly property bool isSelected: root.sameDay(dayDate, root.selectedDate)
            readonly property bool weekend: root.highlightWeekends && root.isWeekend(dayDate)
            readonly property var dotColors: {
                root.revision;
                return root.dotColorsFor ? (root.dotColorsFor(dayDate) ?? []) : [];
            }
            readonly property int extraCount: Math.max(0, dotColors.length - root.maxDots)

            x: mirrored ? root.width - root.gridLeft - (index % root.columns) * (root.cellWidth + root.cellGap) - width : root.gridLeft + (index % root.columns) * (root.cellWidth + root.cellGap)
            y: root.weekdayRowHeight + root.cellGap + Math.floor(index / root.columns) * (root.cellHeight + root.cellGap)
            width: root.cellWidth
            height: root.cellHeight
            radius: Math.min(pressed && Style.shapeMorph ? Style.cornerRadiusXS : root.cellRadius, Math.min(width, height) / 2)

            Behavior on radius {
                enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                DAnim {
                    duration: Style.expressiveDurations.expressiveEffects
                    easing.bezierCurve: Style.expressiveCurves.standard
                }
            }
            color: isSelected ? Style.primary : (inMonth ? Style.foregroundColor(Style.chipSurface, root.floatingWindow) : Style.withAlpha(Style.chipSurface, Style.stateLayerFocus))
            border.width: isToday && !isSelected ? Style.outlineWidthFocused : 0
            border.color: Style.primary

            StyledText {
                anchors.top: parent.top
                anchors.topMargin: Style.spacingXS
                anchors.horizontalCenter: parent.horizontalCenter
                text: cell.dayDate.getDate()
                font.pixelSize: Style.fontSizeMedium
                color: {
                    if (cell.isSelected)
                        return Style.onPrimary;
                    if (cell.isToday)
                        return Style.primary;
                    if (!cell.inMonth)
                        return Style.onSurface_38;
                    if (cell.weekend)
                        return root.weekendColor;
                    return Style.surfaceText;
                }
            }

            Row {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: Style.spacingXS
                spacing: Style.spacingXXS
                visible: cell.dotColors.length > 0

                Repeater {
                    model: cell.dotColors.slice(0, root.maxDots)

                    Rectangle {
                        required property var modelData

                        anchors.verticalCenter: parent.verticalCenter
                        width: Style.spacingXS
                        height: Style.spacingXS
                        radius: Style.fullRadius(width, height)
                        color: modelData
                    }
                }

                StyledText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "+" + cell.extraCount
                    font.pixelSize: Style.fontSizeSmall
                    font.weight: Style.fontWeightMedium
                    color: cell.isSelected ? Style.onPrimary : Style.primary
                    visible: cell.extraCount > 0
                }
            }

            StateLayer {
                id: cellLayer
                control: cell
                stateColor: cell.isSelected ? Style.onPrimary : Style.primary
                disabled: !root.interactive
                enabled: root.interactive
                transitionDuration: Style.expressiveDurations.expressiveEffects
                transitionCurve: Style.expressiveCurves.expressiveEffects
            }
        }
    }
}
