import QtQuick
import QtQuick.Window
import Quickshell.Io
import Quickshell.Widgets
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

Rectangle {
    id: root

    property string imageSource: ""
    property string fallbackIcon: "material:notifications"
    property string fallbackText: ""
    property bool cacheImages: true
    property real ringWidth: 0
    property color ringColor: Style.surfaceVariant
    property bool hasImage: imageSource !== ""
    readonly property string sourceExtension: {
        const name = imageSource.split("?")[0].split("/").pop();
        const dot = name.lastIndexOf(".");
        return dot <= 0 ? "" : name.slice(dot + 1).toLowerCase();
    }
    // AnimatedImage decodes local files on the GUI thread whatever `asynchronous` says (an svg avatar rasterizes at 4000x4000), so extensionless local files (AccountsService icons) get sniffed by magic bytes first.
    readonly property string sniffPath: {
        if (imageSource === "" || sourceExtension !== "")
            return "";
        if (imageSource.startsWith("/"))
            return imageSource;
        if (!imageSource.startsWith("file:///"))
            return "";
        const path = imageSource.slice(7).split("?")[0];
        try {
            return decodeURIComponent(path);
        } catch (e) {
            return path;
        }
    }
    property string sniffedSource: ""
    property bool sniffedAnimatable: false
    readonly property bool shouldProbe: /^(gif|webp|apng)$/.test(sourceExtension) || (sniffedAnimatable && sniffedSource === imageSource)
    property string settledSource: ""
    property string animatedSource: ""
    readonly property bool probeSettled: settledSource !== "" && settledSource === imageSource
    readonly property bool isAnimated: animatedSource !== "" && animatedSource === imageSource
    readonly property var activeImage: {
        if (isAnimated)
            return probe;
        if (staticImage.status === Image.Ready)
            return staticImage;
        if (probe.status === Image.Ready && probe.source !== "")
            return probe;
        return staticImage;
    }
    property int imageStatus: activeImage.status

    signal imageSaved(string filePath)

    function settleSniff(animatable) {
        sniffedAnimatable = animatable;
        sniffedSource = imageSource;
    }

    function animatableBytes(buffer) {
        const b = new Uint8Array(buffer);
        const tag = (offset, text) => {
            for (let i = 0; i < text.length; i++) {
                if (b[offset + i] !== text.charCodeAt(i))
                    return false;
            }
            return true;
        };
        if (b.length < 16)
            return false;
        if (tag(0, "GIF8"))
            return true;
        if (tag(0, "RIFF") && tag(8, "WEBP"))
            return true;
        if (!tag(1, "PNG"))
            return false;
        let offset = 8;
        while (offset + 8 <= b.length) {
            const length = ((b[offset] << 24) | (b[offset + 1] << 16) | (b[offset + 2] << 8) | b[offset + 3]) >>> 0;
            if (tag(offset + 4, "acTL"))
                return true;
            if (tag(offset + 4, "IDAT") || tag(offset + 4, "IEND"))
                return false;
            offset += 12 + length;
        }
        return false;
    }

    function settleProbe() {
        if (probe.status === Image.Error) {
            settledSource = probe.source;
            return;
        }
        if (probe.status !== Image.Ready)
            return;
        animatedSource = probe.frameCount > 1 ? probe.source : "";
        settledSource = probe.source;
    }

    property string _pendingSavePath: ""
    property var _attachedWindow: root.Window.window

    on_AttachedWindowChanged: {
        if (_attachedWindow && _pendingSavePath !== "") {
            Qt.callLater(function () {
                if (root._pendingSavePath !== "") {
                    let path = root._pendingSavePath;
                    root._pendingSavePath = "";
                    root.saveImageToFile(path);
                }
            });
        }
    }

    function saveImageToFile(filePath) {
        if (activeImage.status !== Image.Ready)
            return false;

        if (!activeImage.Window.window) {
            _pendingSavePath = filePath;
            return true;
        }

        activeImage.grabToImage(function (result) {
            if (result && result.saveToFile(filePath)) {
                root.imageSaved(filePath);
            }
        });
        return true;
    }

    radius: width / 2
    color: Style.primaryHover
    border.color: "transparent"
    border.width: 0

    Loader {
        active: root.sniffPath !== "" && root.sniffedSource !== root.imageSource
        sourceComponent: FileView {
            path: root.sniffPath
            printErrors: false
            onLoaded: Qt.callLater(root.settleSniff, root.animatableBytes(data()))
            onLoadFailed: Qt.callLater(root.settleSniff, false)
        }
    }

    // ClippingRectangle's edge is not antialiased; an opaque ring straddling it hides the stair-step.
    Rectangle {
        anchors.fill: parent
        anchors.margins: clipArea.anchors.margins - root.ringWidth / 2
        z: 1
        radius: width / 2
        color: "transparent"
        border.width: root.ringWidth
        border.color: root.ringColor
        visible: root.ringWidth > 0
    }

    ClippingRectangle {
        id: clipArea
        anchors.fill: parent
        anchors.margins: root.ringWidth > 0 ? 2 : 0
        radius: Math.min(width, height) / 2
        color: "transparent"

        // Probes as AnimatedImage to read frameCount; retires once staticImage is ready.
        AnimatedImage {
            id: probe
            anchors.fill: parent
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            cache: root.cacheImages
            visible: root.activeImage === probe && probe.status === Image.Ready && root.imageSource !== ""
            source: root.shouldProbe && staticImage.status !== Image.Ready ? root.imageSource : ""
            // Local files load synchronously inside the source write, so settling here would re-enter the source binding.
            onStatusChanged: Qt.callLater(root.settleProbe)
        }

        // Takes over once the probe settles on a non-animated image.
        Image {
            id: staticImage
            anchors.fill: parent
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            cache: root.cacheImages
            visible: root.activeImage === staticImage && staticImage.status === Image.Ready && root.imageSource !== ""
            sourceSize.width: Math.max(1, Math.ceil(width * Screen.devicePixelRatio))
            sourceSize.height: Math.max(1, Math.ceil(height * Screen.devicePixelRatio))
            source: !root.shouldProbe || (root.probeSettled && !root.isAnimated) ? root.imageSource : ""
        }
    }

    AppIconRenderer {
        anchors.centerIn: parent
        width: Math.round(parent.width * 0.75)
        height: width
        visible: (root.activeImage.status !== Image.Ready || root.imageSource === "") && root.fallbackIcon !== ""
        iconValue: root.fallbackIcon
        iconSize: width
        iconColor: Style.surfaceVariantText
        materialIconSizeAdjustment: 0
        fallbackText: root.fallbackText
        fallbackBackgroundColor: "transparent"
        fallbackTextColor: Style.surfaceVariantText
    }

    StyledText {
        anchors.centerIn: parent
        visible: root.imageSource === "" && root.fallbackIcon === "" && root.fallbackText !== ""
        text: root.fallbackText
        font.pixelSize: Math.max(12, parent.width * 0.5)
        font.weight: Style.fontWeightMedium
        color: Style.surfaceVariantText
    }
}
