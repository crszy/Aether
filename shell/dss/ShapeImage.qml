// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// A picture (album art ...) cut into one of the soft shapes of Blob.qml: cookie, flower, circle ...
// Drawn on a canvas clipped by the shape, so it needs no graphics effects.
Item {
    id: si
    property string source: ""
    property string kind: "cookie"
    property color empty: "#2a2c30"
    property string fallbackIcon: "music_note"
    property string fallbackGlyph: ""
    property color fallbackColor: "#8a8a8f"
    property string loaded: ""
    Blob {
        id: shape
        anchors.fill: parent
        kind: si.kind
        color: si.empty
        visible: si.loaded === "" || si.loaded !== si.source
        MIcon { anchors.centerIn: parent; size: parent.width * 0.3; name: si.fallbackIcon; glyph: si.fallbackGlyph; color: si.fallbackColor }
    }
    Canvas {
        id: cv
        anchors.fill: parent
        visible: si.loaded !== "" && si.loaded === si.source
        renderStrategy: Canvas.Cooperative
        onImageLoaded: { if (isImageLoaded(si.source)) { si.loaded = si.source; requestPaint() } }
        onPaint: {
            const c = getContext("2d"); c.reset()
            if (si.source === "" || !isImageLoaded(si.source)) return
            shape.trace(c, width, height, 1)
            c.clip()
            // cover: fill the shape, keep the picture's proportions
            const im = c.createImageData(si.source)
            const iw = im && im.width ? im.width : width, ih = im && im.height ? im.height : height
            const k = Math.max(width / iw, height / ih)
            c.drawImage(si.source, (width - iw * k) / 2, (height - ih * k) / 2, iw * k, ih * k)
        }
        onWidthChanged: requestPaint()
    }
    onSourceChanged: {
        if (source === "") { loaded = ""; return }
        if (cv.isImageLoaded(source)) { loaded = source; cv.requestPaint() }
        else cv.loadImage(source)
    }
    onKindChanged: cv.requestPaint()
    Component.onCompleted: if (source !== "") { if (cv.isImageLoaded(source)) { loaded = source; cv.requestPaint() } else cv.loadImage(source) }
    Component.onDestruction: if (source !== "") cv.unloadImage(source)
}
