// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import QtQuick

// One notification, in the look of the control center: the app's icon inside your shape, the app and the time,
// the title, the text, a picture when it sent one, its buttons and a close button.
// As a pop-up it grows in from its side (Material 3 emphasized-decelerate), shows how long it stays as a thin
// line that runs out, waits longer for what matters (the pointer on it keeps it) and slips away when it goes.
Item {
    id: nc
    property var n: null
    property var nf: null                // Notifs.qml: theme, sizes
    property bool popup: false
    property bool out: false             // on its way out
    signal closed()
    signal timedOut()
    NPal { id: fallback }
    readonly property var pal: nf && nf.pal ? nf.pal : fallback
    readonly property bool urgent: n && n.urgency === 2
    readonly property var made: new Date()
    readonly property int fs: nf ? nf.fontSize : 13
    readonly property bool fromRight: !nf || nf.posX !== "left"
    readonly property string img: n && n.image && (!nf || nf.images) ? n.image : ""
    implicitHeight: body.implicitHeight + 30
    height: implicitHeight

    // in and out: t runs evenly, the card eases on its own
    property real t: 0
    function ease(p) { return 1 - Math.pow(1 - p, 4) }
    readonly property real et: ease(t)
    Component.onCompleted: { t = 1; if (waits) lifeAnim.restart() }
    onOutChanged: if (out) t = 0
    Behavior on t { NumberAnimation { duration: nc.out ? 240 : 640; easing.type: Easing.Linear } }
    opacity: nc.popup ? Math.min(1, et * 1.4) : 1
    scale: nc.popup ? 0.92 + 0.08 * et : 1
    transformOrigin: fromRight ? Item.Right : Item.Left
    transform: Translate { x: nc.popup ? (1 - nc.et) * (nc.fromRight ? 70 : -70) : 0 }

    // how long it stays
    readonly property int life: n && n.expireTimeout > 0 ? Math.max(3000, n.expireTimeout) : (nf ? nf.timeout * 1000 : 6000)
    readonly property bool waits: nc.popup && !nc.out && !hov.hovered && !(nc.urgent && (!nf || nf.sticky))
    property real remain: 1
    NumberAnimation { id: lifeAnim; target: nc; property: "remain"; to: 0; duration: nc.life }
    onWaitsChanged: { if (waits) { lifeAnim.duration = Math.max(400, nc.life * remain); lifeAnim.restart() } else lifeAnim.stop() }
    onRemainChanged: if (remain <= 0.001 && nc.waits) nc.timedOut()

    Shadow { anchors.fill: bg; radius: bg.radius; on: nc.popup && Look.shadows && Look.shadowNotif; size: Look.shadowSize * 0.75; strength: Look.shadowStrength * 0.9 }
    Rectangle {
        id: bg
        anchors.fill: parent
        radius: nc.nf ? nc.nf.radius : 26
        color: nc.popup ? nc.pal.surface : nc.pal.card
        border.width: nc.urgent ? 1.5 : (nc.popup ? 1 : 0)
        border.color: nc.urgent ? "#FFB4AB" : nc.pal.line
        clip: true
        // the time it has left, as a thin line along the bottom
        Rectangle {
            visible: nc.popup && !(nc.urgent && (!nc.nf || nc.nf.sticky))
            anchors.bottom: parent.bottom; anchors.bottomMargin: 6
            x: 18; height: 3; radius: 2
            width: Math.max(0, (parent.width - 36) * nc.remain)
            color: Qt.rgba(nc.pal.primary.r, nc.pal.primary.g, nc.pal.primary.b, 0.55)
        }
    }
    HoverHandler { id: hov }

    // the icon, in your shape; a picture (a profile photo, an avatar) fills a circle instead
    readonly property string iconSrc: {
        const n = nc.n
        if (!n) return ""
        if (n.appIcon && n.appIcon !== "") return n.appIcon.indexOf("/") === 0 ? "file:///" + n.appIcon : Icons.get(n.appIcon, true)
        return Icons.get((n.appName || "dialog-information").toLowerCase(), true)
    }
    readonly property bool picture: {
        const a = nc.n && nc.n.appIcon ? String(nc.n.appIcon) : ""
        return a.indexOf("/") === 0 || a.indexOf("file:") === 0 || a.indexOf("image://") === 0
    }
    Item {
        id: ic
        x: 16; y: 16; width: 46; height: 46
        Blob { anchors.fill: parent; kind: Look.shape; visible: !nc.picture || icImg.status !== Image.Ready
            color: nc.urgent ? Qt.rgba(1, 0.42, 0.42, 0.35) : nc.pal.container
            SlowSpin { turn: 360; period: 30000; running: nc.popup && !Look.lite } }
        Image {
            id: icImg
            anchors.centerIn: parent
            width: 26; height: 26
            sourceSize.width: 64; sourceSize.height: 64
            source: nc.iconSrc
            visible: !nc.picture && status === Image.Ready
        }
        RoundImage {
            anchors.fill: parent
            visible: nc.picture && icImg.status === Image.Ready
            source: nc.picture ? nc.iconSrc : ""
            cornerRadius: width / 2
        }
        MIcon { anchors.centerIn: parent; size: 20; filled: true; name: nc.urgent ? "warning" : "notifications"; glyph: ""
            color: nc.urgent ? "#FFB4AB" : nc.pal.inkContainer; visible: icImg.status !== Image.Ready }
    }
    Column {
        id: body
        x: 76; y: 15
        width: parent.width - 76 - 44
        spacing: 3
        Row {
            spacing: 6
            Text { text: nc.n ? (nc.n.appName || "Notification") : ""; color: nc.pal.dim; font.family: Fonts.main; font.pixelSize: nc.fs - 2 }
            Text { text: "·  " + nc.made.toLocaleTimeString(Qt.locale(), Locale.ShortFormat); color: nc.pal.dim; font.family: Fonts.main; font.pixelSize: nc.fs - 2 }
        }
        Text { width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            text: nc.n ? (nc.n.summary || "") : ""; color: nc.pal.fg; font.family: Fonts.main; font.pixelSize: nc.fs + 1; font.weight: Font.DemiBold }
        Text { width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: nc.popup ? 3 : 6; elide: Text.ElideRight; visible: text !== ""
            textFormat: Text.StyledText
            text: nc.n ? (nc.n.body || "") : ""; color: nc.pal.dim; font.family: Fonts.main; font.pixelSize: nc.fs - 1
            onLinkActivated: function(l) { DssWin.run(["xdg-open", l]) } }
        // a picture it sent (a screenshot, a cover ...)
        Item {
            visible: nc.img !== ""
            width: parent.width; height: visible ? 126 : 0
            RoundImage { anchors.fill: parent; anchors.topMargin: 6; cornerRadius: 16; source: nc.img }
        }
        Flow {
            width: parent.width
            spacing: 6
            topPadding: 6
            visible: nc.n && nc.n.actions && nc.n.actions.length > 0
            Repeater {
                model: nc.n && nc.n.actions ? nc.n.actions : []
                delegate: Rectangle {
                    width: at.implicitWidth + 26; height: 32; radius: am.pressed ? 10 : 16
                    color: am.containsMouse ? nc.pal.primary : nc.pal.container
                    Behavior on radius { NumberAnimation { duration: 200 } }
                    Behavior on color { ColorAnimation { duration: 140 } }
                    Text { id: at; anchors.centerIn: parent; text: modelData.text; color: am.containsMouse ? nc.pal.inkPrimary : nc.pal.inkContainer; font.family: Fonts.main; font.pixelSize: nc.fs - 1; font.weight: Font.Medium }
                    MouseArea { id: am; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { modelData.invoke(); nc.closed() } }
                }
            }
        }
    }
    Rectangle {
        anchors.right: parent.right; anchors.rightMargin: 12; y: 12
        width: 30; height: 30; radius: 15
        color: xm.containsMouse ? nc.pal.cardHi : "transparent"
        MIcon { anchors.centerIn: parent; size: 16; name: "close"; glyph: ""; color: nc.pal.dim }
        MouseArea { id: xm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: nc.closed() }
    }
}
