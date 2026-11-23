// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Wayland
import QtQuick
import qs.services as Cae

// Reads how bright your wallpaper is in each part of the screen (a 64 x 36 grid) and gives it to Look.lumGrid.
// The widgets use it to choose dark text on a light wallpaper and light text on a dark one.
// It lives in a 2 x 2 pixel surface at the very back, takes no input and does nothing between wallpaper changes.
PanelWindow {
    screen: Mon.widgetScreen
    id: wl
    property string path: ""
    property real sw: 1920
    property real sh: 1080
    property real ratio: 16 / 9      // width / height of the wallpaper picture

    visible: Look.adapt && path !== "" && !Cae.WinSettings.weShown
    implicitWidth: 2
    implicitHeight: 2
    color: "transparent"
    anchors { left: true; top: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Background
    WlrLayershell.namespace: "rice-wallum"
    mask: Region { }

    // only to learn the shape of the picture (decoded small)
    Image {
        id: probe
        visible: false
        asynchronous: true
        cache: false
        sourceSize.width: 64
        source: wl.path !== "" ? "file:///" + wl.path : ""
        onStatusChanged: if (status === Image.Ready && implicitHeight > 0) { wl.ratio = implicitWidth / implicitHeight; cv.requestPaint() }
    }

    Canvas {
        id: cv
        width: Look.lumW
        height: Look.lumH
        opacity: 0.01
        renderTarget: Canvas.Image
        property string src: wl.path !== "" ? "file:///" + wl.path : ""
        property string loadedSrc: ""
        onSrcChanged: {
            if (loadedSrc !== "") unloadImage(loadedSrc)
            loadedSrc = src
            if (src !== "") loadImage(src)
        }
        Component.onCompleted: if (src !== "") { loadedSrc = src; loadImage(src) }
        onImageLoaded: requestPaint()
        onPaint: {
            if (src === "" || !isImageLoaded(src)) return
            const c = getContext("2d")
            c.reset()
            const W = Look.lumW, H = Look.lumH
            // fill the screen like the wallpaper does: the picture covers it, the extra is cut off
            const sa = wl.sw / wl.sh, ra = wl.ratio
            // the grid is stretched to the screen, so measure the picture in screen shape:
            // wider than the screen: height fits, width is cut; taller: width fits, height is cut
            const dW = ra > sa ? W * ra / sa : W
            const dH = ra > sa ? H : H * sa / ra
            c.drawImage(src, (W - dW) / 2, (H - dH) / 2, dW, dH)
            const px = c.getImageData(0, 0, W, H).data
            const g = new Array(W * H)
            for (let i = 0; i < g.length; i++) g[i] = (0.2126 * px[i * 4] + 0.7152 * px[i * 4 + 1] + 0.0722 * px[i * 4 + 2]) / 255
            Look.lumGrid = g
        }
    }
}
