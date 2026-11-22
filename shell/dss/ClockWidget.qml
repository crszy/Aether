// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

Item {
    id: ck
    property string clockStyle: "digital"
    property string mode: "draggable"
    property string wallpaper: ""
    property var pal: null
    property string fontKey: "default"      // the lock screen clock fonts (rice-fonts downloads them)
    property int bw: 1920
    property int bh: 1080
    property date now: new Date()
    property real fx: -1
    property real fy: -1
    readonly property int natW: clockStyle === "cookie" ? 260 : (clockStyle === "pixel" ? 270 : (clockStyle === "line" ? 430 : 200))
    readonly property int natH: clockStyle === "cookie" ? 260 : (clockStyle === "pixel" ? 100 : (clockStyle === "line" ? 190 : 310))
    readonly property var fonts: ({ bebas: "Bebas Neue", orbitron: "Orbitron", outfit: "Outfit", grotesk: "Space Grotesk",
                                    unbounded: "Unbounded", shoulders: "Big Shoulders Display" })
    readonly property string fam: fonts[fontKey] !== undefined ? fonts[fontKey] : Fonts.family("number")
    readonly property bool lightBg: pal ? pal.light : false
    readonly property color ink: pal ? pal.fg : "#F6F5F4"
    readonly property color hand: pal ? pal.primary : "#F6F5F4"
    // frosted like the other widgets when blur is on (Hyprland: its layer rule; niri: the round region below)
    readonly property bool glass: Look.blur && !Look.flat
    onGlassChanged: cookie.requestPaint()
    onLightBgChanged: { cookie.requestPaint(); pixel.requestPaint() }
    onHandChanged: { cookie.requestPaint(); pixel.requestPaint() }

    function pad(n) { return n < 10 ? "0" + n : "" + n }
    readonly property string hh: pad(now.getHours())
    readonly property string mm: pad(now.getMinutes())

    Timer { interval: 1000; running: true; repeat: true; onTriggered: ck.now = new Date() }

    // ---- digital
    Column {
        visible: ck.clockStyle === "digital"
        anchors.centerIn: parent
        spacing: -42
        T { text: ck.hh; font.family: ck.fam; font.pixelSize: 150; font.bold: true; color: ck.hand; opacity: 0.9; anchors.horizontalCenter: parent.horizontalCenter }
        T { text: ck.mm; font.family: ck.fam; font.pixelSize: 150; font.bold: true; color: ck.ink; opacity: ck.lightBg ? 0.6 : 0.45; anchors.horizontalCenter: parent.horizontalCenter }
    }

    // ---- line: hours and minutes side by side, the date under them
    Column {
        visible: ck.clockStyle === "line"
        anchors.centerIn: parent
        spacing: -6
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 0
            T { text: ck.hh; font.family: ck.fam; font.pixelSize: 128; font.bold: true; color: ck.hand; opacity: 0.92 }
            T { text: ":"; font.family: ck.fam; font.pixelSize: 128; font.bold: true; color: ck.ink; opacity: 0.4; anchors.verticalCenter: parent.verticalCenter }
            T { text: ck.mm; font.family: ck.fam; font.pixelSize: 128; font.bold: true; color: ck.ink; opacity: ck.lightBg ? 0.62 : 0.5 }
        }
        T {
            anchors.horizontalCenter: parent.horizontalCenter
            text: ck.now.toLocaleDateString(Tr.loc, "dddd, d MMMM")
            font.pixelSize: 19
            color: ck.pal ? ck.pal.fg2 : "#c8c8cc"
        }
    }

    // ---- cookie
    // niri blurs only behind the cards a widget names (Card.qml): this round one sits just inside the cookie's
    // waves, so no blur shows outside the shape
    Rectangle {
        id: cookieBlur
        visible: ck.clockStyle === "cookie" && ck.glass
        anchors.centerIn: cookie
        width: 212; height: 212; radius: 106
        readonly property real blurRadius: 106
        color: "#20000000"
        opacity: 0
        property var holderItem: null
        Component.onCompleted: {
            for (let p = parent; p; p = p.parent) if (p.widgetHolder === true) { holderItem = p; p.cards = p.cards.concat([this]); break }
        }
        Component.onDestruction: if (holderItem) { const me = this; holderItem.cards = holderItem.cards.filter(c => c !== me) }
    }
    Canvas {
        id: cookie
        visible: ck.clockStyle === "cookie"
        anchors.centerIn: parent
        width: 250
        height: 250
        property real sec: ck.now.getSeconds()
        onSecChanged: requestPaint()
        onVisibleChanged: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            const cx = 125, cy = 125, R = 112
            c.fillStyle = ck.lightBg ? (ck.glass ? "rgba(248,248,250,0.55)" : "rgba(248,248,250,0.85)")
                                     : (ck.glass ? "rgba(30,30,34,0.5)" : "rgba(44,44,48,0.82)")
            c.beginPath()
            for (let i = 0; i <= 360; i++) {
                const a = i * Math.PI / 180
                const r = R * (1 + 0.05 * Math.cos(12 * a))
                const x = cx + r * Math.cos(a), y = cy + r * Math.sin(a)
                if (i === 0) c.moveTo(x, y); else c.lineTo(x, y)
            }
            c.closePath()
            c.fill()
            c.fillStyle = ck.lightBg ? "rgba(0,0,0,0.35)" : "rgba(255,255,255,0.35)"
            for (let h = 0; h < 12; h++) {
                const a = h * Math.PI / 6
                c.beginPath()
                c.arc(cx + 88 * Math.sin(a), cy - 88 * Math.cos(a), h % 3 === 0 ? 4 : 2.5, 0, Math.PI * 2)
                c.fill()
            }
            const d = ck.now
            const hAng = ((d.getHours() % 12) + d.getMinutes() / 60) * Math.PI / 6
            const mAng = (d.getMinutes() + d.getSeconds() / 60) * Math.PI / 30
            const sAng = d.getSeconds() * Math.PI / 30
            c.lineCap = "round"
            c.strokeStyle = ck.lightBg ? "#15151a" : "#F6F5F4"
            c.lineWidth = 9
            c.beginPath(); c.moveTo(cx, cy); c.lineTo(cx + 46 * Math.sin(hAng), cy - 46 * Math.cos(hAng)); c.stroke()
            c.lineWidth = 6
            c.beginPath(); c.moveTo(cx, cy); c.lineTo(cx + 72 * Math.sin(mAng), cy - 72 * Math.cos(mAng)); c.stroke()
            c.strokeStyle = ck.hand
            c.globalAlpha = 0.85
            c.lineWidth = 2
            c.beginPath(); c.moveTo(cx, cy); c.lineTo(cx + 82 * Math.sin(sAng), cy - 82 * Math.cos(sAng)); c.stroke()
            c.globalAlpha = 1
            c.fillStyle = ck.hand
            c.beginPath(); c.arc(cx, cy, 6, 0, Math.PI * 2); c.fill()
        }
    }

    // ---- pixel
    Canvas {
        id: pixel
        visible: ck.clockStyle === "pixel"
        anchors.centerIn: parent
        readonly property int cell: 14
        width: 17 * cell
        height: 5 * cell
        property string txt: ck.hh + ck.mm
        onTxtChanged: requestPaint()
        onVisibleChanged: requestPaint()
        readonly property var glyphs: ({
            "0": ["111","101","101","101","111"], "1": ["010","110","010","010","111"],
            "2": ["111","001","111","100","111"], "3": ["111","001","111","001","111"],
            "4": ["101","101","111","001","001"], "5": ["111","100","111","001","111"],
            "6": ["111","100","111","101","111"], "7": ["111","001","001","001","001"],
            "8": ["111","101","111","101","111"], "9": ["111","101","111","001","111"],
            ":": ["0","1","0","1","0"]
        })
        onPaint: {
            const c = getContext("2d")
            c.reset()
            const seq = [txt[0], txt[1], ":", txt[2], txt[3]]
            let x = 0
            c.fillStyle = ck.ink
            for (let s = 0; s < seq.length; s++) {
                const g = glyphs[seq[s]]
                if (!g) continue
                for (let r = 0; r < g.length; r++)
                    for (let q = 0; q < g[r].length; q++)
                        if (g[r][q] === "1") {
                            c.globalAlpha = s < 2 ? 0.9 : (s === 2 ? 0.6 : 0.5)
                            c.fillRect((x + q) * cell + 1, r * cell + 1, cell - 2, cell - 2)
                        }
                x += g[0].length + 1
            }
        }
    }

    // ---- wallpaper busyness analysis (used by "least/most busy")
    Canvas {
        id: an
        width: 128
        height: 72
        opacity: 0.01
        z: -1
        renderTarget: Canvas.Image
        property string src: (ck.mode !== "draggable" && ck.wallpaper !== "") ? "file:///" + ck.wallpaper : ""
        onSrcChanged: if (src !== "") loadImage(src)
        Component.onCompleted: if (src !== "") loadImage(src)
        onImageLoaded: requestPaint()
        onPaint: {
            if (src === "" || !isImageLoaded(src)) return
            const c = getContext("2d")
            c.drawImage(src, 0, 0, width, height)
            const px = c.getImageData(0, 0, width, height).data
            const g = new Array(width * height)
            for (let i = 0; i < g.length; i++) g[i] = 0.3 * px[i * 4] + 0.59 * px[i * 4 + 1] + 0.11 * px[i * 4 + 2]
            const E = [0, 0, 0, 0, 0, 0, 0, 0, 0]
            for (let y = 1; y < height; y++)
                for (let x = 1; x < width; x++) {
                    const e = Math.abs(g[y * width + x] - g[y * width + x - 1]) + Math.abs(g[y * width + x] - g[(y - 1) * width + x])
                    const cx = Math.min(2, Math.floor(x / (width / 3)))
                    const cy = Math.min(2, Math.floor(y / (height / 3)))
                    E[cy * 3 + cx] += e
                }
            let best = 4
            for (let i = 0; i < 9; i++) {
                if (ck.mode === "leastBusy" ? E[i] < E[best] : E[i] > E[best]) best = i
            }
            ck.fx = ((best % 3) + 0.5) / 3
            ck.fy = (Math.floor(best / 3) + 0.5) / 3
        }
    }
}
