// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import QtQuick

// Toolbox > Convert: units and money. Pick what kind, type the amount, choose from and to; or simply type it
// whole ("10 km in mi", "100 usd pln") in the amount field. A click on the result copies it.
Item {
    id: cv
    property var pal
    // the opening wave (Tools.qml): 0 .. 1; each part comes a moment after the one above it
    property real enter: 1
    function e(i) { const p = Math.max(0, Math.min(1, (enter - i * 0.07) / 0.6)); return 1 - Math.pow(1 - p, 4) }
    property bool active: false
    property int gi: 0
    readonly property var group: Conv.groups[gi]
    property string from: "km"
    property string to: "mi"
    property bool copied: false
    function focusIt() { amount.input.forceActiveFocus() }
    readonly property string summary: Tr.t("Units and money")
    onActiveChanged: if (active && group.key === "money") Conv.ask()
    function pickGroup(i) {
        gi = i
        const l = Conv.groups[i].list
        from = l[Math.min(1, l.length - 1)]; to = l[Math.min(2, l.length - 1)]
        if (Conv.groups[i].key === "length") { from = "km"; to = "mi" }
        if (Conv.groups[i].key === "temperature") { from = "c"; to = "f" }
        if (Conv.groups[i].key === "money") { Conv.ask(); from = "EUR"; to = "PLN" }
        copied = false
    }
    // typed whole ("10 km in mi"): that wins; else the number with the chosen units
    readonly property var typed: Conv.parse(amount.text)
    readonly property real num: parseFloat(amount.text.replace(/\s/g, "").replace(",", "."))
    readonly property string result: {
        if (typed) return typed.text
        if (isNaN(num)) return ""
        const r = Conv.convert(num, from, to)
        if (isNaN(r)) return ""
        return Conv.fmt(group.key === "money" ? Math.round(r * 100) / 100 : r) + " " + Conv.label(to)
    }
    readonly property string resultFrom: typed ? typed.from : (isNaN(num) ? "" : Conv.fmt(num) + " " + Conv.label(from))
    readonly property bool noRates: group.key === "money" && Object.keys(Conv.rates).length === 0
    onResultChanged: copied = false

    Column {
        width: parent.width; spacing: 16
        // what kind
        Flow {
            width: parent.width; spacing: 8
            opacity: cv.e(0); transform: Translate { y: (1 - cv.e(0)) * 22 }
            Repeater {
                model: Conv.groups
                delegate: ToolBtn { required property var modelData; required property int index; pal: cv.pal; h: 40
                    icon: modelData.sym; label: cv.gi === index ? Tr.t(modelData.key.charAt(0).toUpperCase() + modelData.key.slice(1)) : ""
                    on: cv.gi === index; onClicked: cv.pickGroup(index) }
            }
        }
        ToolField { id: amount; pal: cv.pal; width: parent.width; numbers: true; text: "1"
        opacity: cv.e(1); transform: Translate { y: (1 - cv.e(1)) * 22 }
            hint: Tr.t("Amount, or \"10 km in mi\""); icon: "calculate"; glyph: String.fromCodePoint(0xF00EC) }
        // from and to
        Repeater {
            model: [{ t: "From", k: "from" }, { t: "To", k: "to" }]
            delegate: Column {
                required property var modelData
                width: parent.width; spacing: 8
                property real dimK: cv.typed ? 0.35 : 1
                Behavior on dimK { NumberAnimation { duration: 200 } }
                opacity: dimK * cv.e(2); transform: Translate { y: (1 - cv.e(2)) * 22 }
                Text { text: Tr.t(parent.modelData.t); color: cv.pal.dim; font.family: Fonts.main; font.pixelSize: 12 }
                Flow {
                    width: parent.width; spacing: 6
                    Repeater {
                        model: cv.group.list
                        delegate: ToolBtn { required property string modelData; pal: cv.pal; h: 34; label: Conv.label(modelData)
                            on: (parent.parent.modelData.k === "from" ? cv.from : cv.to) === modelData
                            onClicked: { if (parent.parent.modelData.k === "from") cv.from = modelData; else cv.to = modelData } }
                    }
                }
            }
        }
        // the answer (a click copies it), and swap
        Rectangle {
            width: parent.width; height: 116; radius: am.pressed ? 22 : 30
            opacity: cv.e(3); transform: Translate { y: (1 - cv.e(3)) * 22 }
            Behavior on radius { NumberAnimation { duration: 180 } }
            color: cv.pal.container
            Column {
                x: 22; anchors.verticalCenter: parent.verticalCenter; spacing: 4; width: parent.width - 100
                Text { text: cv.noRates ? Tr.t("No rates yet") : (cv.resultFrom !== "" ? cv.resultFrom + " =" : ""); color: cv.pal.inkContainer; opacity: 0.75
                    font.family: Fonts.main; font.pixelSize: 13 }
                Text { width: parent.width; text: cv.noRates ? Tr.t("They come from the internet, once") : (cv.result !== "" ? cv.result : "—")
                    color: cv.pal.inkContainer; font.family: Fonts.numbers; font.pixelSize: cv.noRates ? 14 : 30; font.weight: Font.DemiBold
                    fontSizeMode: Text.HorizontalFit; minimumPixelSize: 14; elide: Text.ElideRight }
                Text { color: cv.pal.inkContainer; opacity: 0.6; font.family: Fonts.main; font.pixelSize: 11
                    text: cv.copied ? Tr.t("Copied") : (cv.group.key === "money" && Conv.ratesDate !== "" ? Tr.t("European Central Bank") + ", " + Conv.ratesDate : Tr.t("Click to copy")) }
            }
            MouseArea { id: am; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; enabled: cv.result !== ""
                onClicked: { DssWin.run(["bash", "-c", "printf %s \"$1\" | wl-copy", "_", cv.result.split(" ")[0]]); Sfx.play("select"); cv.copied = true } }
            Rectangle {
                id: swap
                anchors.right: parent.right; anchors.rightMargin: 18; anchors.verticalCenter: parent.verticalCenter
                width: 52; height: 52; radius: sm.pressed ? 16 : 26
                Behavior on radius { NumberAnimation { duration: 160 } }
                color: sm.containsMouse ? Qt.lighter(cv.pal.primary, 1.07) : cv.pal.primary
                visible: !cv.typed
                property real turn: 0
                Behavior on turn { NumberAnimation { duration: 420; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
                MIcon { anchors.centerIn: parent; size: 24; name: "swap_vert"; glyph: String.fromCodePoint(0xF04E1); color: cv.pal.inkPrimary; rotation: swap.turn }
                MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { Sfx.play("click"); const f = cv.from; cv.from = cv.to; cv.to = f; swap.turn += 180 } }
            }
        }
    }
}
