import QtQuick
import qs.dms.DCommon.Common

Item {
    id: root

    property string imagePath: ""
    property int maxCacheSize: 512
    property int status: isAnimated ? animatedImg.status : staticImg.status
    property int fillMode: Image.PreserveAspectCrop
    // AnimatedImage decodes full-size on the GUI thread and is never cached;
    // disable for thumbnail grids
    property bool animate: true
    property bool asynchronous: true
    // Remote images are fetched on every load by default; on, a scaled jpeg copy is kept in the image cache keyed by URL.
    property bool cacheRemote: false
    property bool _fromCache: false
    property string _originalSource: ""
    property string _cacheTarget: ""

    readonly property bool isRemoteUrl: imagePath.startsWith("http://") || imagePath.startsWith("https://")
    readonly property bool isAnimated: animate && !!imagePath && hasAnimatedExtension(imagePath)
    readonly property string normalizedPath: {
        if (!imagePath)
            return "";
        if (isRemoteUrl)
            return imagePath;
        if (imagePath.startsWith("file:///"))
            return imagePath.substring(7);
        return imagePath;
    }

    function hasAnimatedExtension(path) {
        const clean = path.split(/[?#]/)[0].toLowerCase();
        return clean.endsWith(".gif") || clean.endsWith(".webp");
    }

    function djb2Hash(str) {
        if (!str)
            return "";
        let hash = 5381;
        for (let i = 0; i < str.length; i++) {
            hash = ((hash << 5) + hash) + str.charCodeAt(i);
            hash = hash & 0x7FFFFFFF;
        }
        return hash.toString(16).padStart(8, '0');
    }

    readonly property string imageHash: normalizedPath ? djb2Hash(normalizedPath) : ""
    readonly property string cacheFileName: {
        if (!imageHash || isAnimated)
            return "";
        if (isRemoteUrl)
            return cacheRemote ? `${imageHash}@${maxCacheSize}x${maxCacheSize}.jpg` : "";
        return `${imageHash}@${maxCacheSize}x${maxCacheSize}.png`;
    }
    readonly property string cachePath: cacheFileName ? `${Paths.stringify(Paths.imagecache)}/${cacheFileName}` : ""
    readonly property string encodedImagePath: {
        if (!normalizedPath)
            return "";
        if (isRemoteUrl)
            return normalizedPath;
        return "file:///" + normalizedPath.split('/').map(s => encodeURIComponent(s)).join('/');
    }

    AnimatedImage {
        id: animatedImg
        anchors.fill: parent
        visible: root.isAnimated
        asynchronous: root.asynchronous
        fillMode: root.fillMode
        source: root.isAnimated ? root.imagePath : ""
        playing: visible && status === AnimatedImage.Ready
    }

    Image {
        id: staticImg
        anchors.fill: parent
        visible: !root.isAnimated
        asynchronous: root.asynchronous
        fillMode: root.fillMode
        smooth: true

        function cacheGrab() {
            if (root._fromCache || !root._cacheTarget || status !== Image.Ready)
                return;
            if (!visible || width <= 0 || height <= 0 || !Window.window?.visible)
                return;
            const grabPath = root._cacheTarget;
            const scale = root.maxCacheSize / Math.max(width, height);
            grabToImage(res => {
                if (grabPath !== root._cacheTarget)
                    return;
                if (res.saveToFile(grabPath))
                    ImageCacheIndex.add(grabPath.substring(grabPath.lastIndexOf("/") + 1));
            }, Qt.size(Math.round(width * scale), Math.round(height * scale)));
        }

        // cache files are already bounded, and a sourceSize box would cover-scale them into a larger texture
        function loadOriginal() {
            root._fromCache = false;
            sourceSize = Qt.size(root.maxCacheSize, root.maxCacheSize);
            source = root._originalSource;
        }

        onStatusChanged: {
            switch (status) {
            case Image.Error:
                if (!root._fromCache)
                    return;
                loadOriginal();
                return;
            case Image.Ready:
                if (!root._cacheTarget)
                    return;
                if (root._fromCache) {
                    // older builds grabbed at item size under the same file name
                    if (Math.max(implicitWidth, implicitHeight) < root.maxCacheSize)
                        loadOriginal();
                    return;
                }
                // A host that shows the image only once it is Ready still reads as hidden here; its binding settles before the next tick.
                Qt.callLater(cacheGrab);
                return;
            }
        }
    }

    // Derives everything from a local snapshot of imagePath: sibling property
    // bindings (isRemoteUrl, encodedImagePath, ...) are still stale when
    // onImagePathChanged runs, so reading them here routes remote URLs down
    // the local-file branch on the first path change
    function resolveSource() {
        const path = imagePath;
        _fromCache = false;
        _cacheTarget = "";
        if (!path) {
            staticImg.source = "";
            return;
        }
        if (animate && hasAnimatedExtension(path))
            return;
        const remote = path.startsWith("http://") || path.startsWith("https://");
        if (remote && !cacheRemote) {
            staticImg.sourceSize = Qt.size(maxCacheSize, maxCacheSize);
            staticImg.source = path;
            return;
        }
        const stripped = remote ? path : path.startsWith("file:///") ? path.substring(7) : path;
        const encoded = remote ? path : "file:///" + stripped.split('/').map(s => encodeURIComponent(s)).join('/');
        const hash = djb2Hash(stripped);
        if (!hash) {
            staticImg.sourceSize = Qt.size(maxCacheSize, maxCacheSize);
            staticImg.source = encoded;
            return;
        }
        const cacheName = `${hash}@${maxCacheSize}x${maxCacheSize}.${remote ? "jpg" : "png"}`;
        _originalSource = encoded;
        _cacheTarget = `${Paths.stringify(Paths.imagecache)}/${cacheName}`;
        if (ImageCacheIndex.ready && !ImageCacheIndex.has(cacheName)) {
            staticImg.loadOriginal();
            return;
        }
        _fromCache = true;
        staticImg.sourceSize = undefined;
        staticImg.source = _cacheTarget;
    }

    onImagePathChanged: resolveSource()
    // During creation onImagePathChanged fires before sibling properties (maxCacheSize) initialize
    onCachePathChanged: resolveSource()
}
