pragma Singleton
import "."
import QtQuick

QtObject {
    property int high: 80
    property int midHigh: 60
    property int mid: 40
    property int low: 20
    property int critical: 10
    property int barMargins: 10
}
