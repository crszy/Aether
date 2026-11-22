import QtQuick
import "../Common/MaterialWallpaper.js" as MaterialWallpaper

Canvas {
    id: root

    required property var palette
    property var composition: MaterialWallpaper.preset(MaterialWallpaper.defaultPreset)
    readonly property real pixelRatio: Window.window?.devicePixelRatio ?? Screen.devicePixelRatio
    property bool ready: false
    readonly property string patternSource: composition.pattern === "none" ? "" : Qt.resolvedUrl("wallpaper-patterns/" + composition.pattern + ".svg")
    property string loadedPattern: ""
    readonly property string renderKey: JSON.stringify([composition, palette, width, height, pixelRatio])

    signal invalidated

    contextType: "2d"
    renderStrategy: Canvas.Immediate

    function repaint() {
        ready = false;
        invalidated();
        requestPaint();
    }

    function paintLayer(ctx, layers, index) {
        const value = layers[index];
        const opacity = value.opacity ?? 1;
        const below = value.overlap === "none" ? [] : layers.slice(0, index);
        const fill = MaterialWallpaper.colorOf(palette, value.fill);
        const overlap = MaterialWallpaper.colorOf(palette, value.overlap);
        ctx.globalAlpha = opacity;
        if (opacity >= 1 || !below.length) {
            MaterialWallpaper.trace(ctx, value, width, height);
            ctx.fillStyle = fill;
            ctx.fill();
            for (const under of below) {
                ctx.save();
                MaterialWallpaper.trace(ctx, under, width, height);
                ctx.clip();
                MaterialWallpaper.trace(ctx, value, width, height);
                ctx.fillStyle = overlap;
                ctx.fill();
                // the base fill already anti-aliased this edge, so filling it again leaves a lighter hairline; a one-pixel stroke repaints it
                ctx.strokeStyle = overlap;
                ctx.lineWidth = 1;
                ctx.lineJoin = "round";
                ctx.stroke();
                ctx.restore();
            }
            ctx.globalAlpha = 1;
            return;
        }
        for (let j = 0; j < below.length; j++) {
            ctx.save();
            MaterialWallpaper.trace(ctx, below[j], width, height);
            ctx.clip();
            for (let k = 0; k < j; k++)
                MaterialWallpaper.clipOutside(ctx, below[k], width, height);
            MaterialWallpaper.trace(ctx, value, width, height);
            ctx.fillStyle = overlap;
            ctx.fill();
            ctx.restore();
        }
        ctx.save();
        for (const under of below)
            MaterialWallpaper.clipOutside(ctx, under, width, height);
        MaterialWallpaper.trace(ctx, value, width, height);
        ctx.fillStyle = fill;
        ctx.fill();
        ctx.restore();
        ctx.globalAlpha = 1;
    }

    onRenderKeyChanged: repaint()
    onAvailableChanged: if (available)
        repaint()
    onPatternSourceChanged: {
        if (loadedPattern)
            unloadImage(loadedPattern);
        loadedPattern = patternSource;
        if (loadedPattern)
            loadImage(loadedPattern);
        repaint();
    }
    onImageLoaded: repaint()
    onPainted: ready = !patternSource || isImageLoaded(patternSource) || isImageError(patternSource)
    onPaint: {
        const ctx = getContext("2d");
        if (!ctx || width <= 0 || height <= 0)
            return;
        ctx.reset();
        if (patternSource && isImageLoaded(patternSource)) {
            ctx.globalAlpha = composition.strength;
            ctx.fillStyle = ctx.createPattern(patternSource, "repeat");
            ctx.fillRect(0, 0, width, height);
            ctx.globalAlpha = 1;
            ctx.globalCompositeOperation = "source-in";
            ctx.fillStyle = palette.primary;
            ctx.fillRect(0, 0, width, height);
            ctx.globalCompositeOperation = "destination-over";
        }
        ctx.fillStyle = palette.surface;
        ctx.fillRect(0, 0, width, height);
        ctx.globalCompositeOperation = "source-over";
        const layers = composition.layers;
        for (let i = 0; i < layers.length; i++)
            paintLayer(ctx, layers, i);
    }
}
