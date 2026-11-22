import QtQuick
import Quickshell

// Apple-style Liquid Glass background for one of Aether's own panels (windows.json "glass": "liquid").
// Aether knows which wallpaper is behind its panels, so the glass samples that picture directly: lens edges,
// light blur, rim light, tint (assets/shaders/liquid_glass.frag). Put it behind a panel's content, size it like
// the panel, and tell it where its window sits on the screen.
//
//   LiquidGlass { anchors.fill: parent; radius: 20; source: wallpaperUrl; screenSize: Qt.size(screen.width, screen.height)
//                 windowOrigin: Qt.point(xOfWindowOnScreen, yOfWindowOnScreen) }
Item {
    id: root

    property url source                       // the wallpaper picture (as drawn: cropped to fill the screen)
    property size screenSize: Qt.size(1920, 1080)
    property point windowOrigin: Qt.point(0, 0)   // where this item's window sits on its screen
    property real radius: 20
    property real bezel: Math.min(28, Math.min(width, height) * 0.45)
    property real refraction: 18
    property real blur: 0.35
    property real rim: 0.55
    property color tint: Qt.rgba(0.08, 0.08, 0.1, 0.28)

    // this item's top-left on the screen; re-read when it or its parent moves or resizes
    property point origin: Qt.point(0, 0)
    function place(): void {
        const p = root.mapToItem(null, 0, 0);
        origin = Qt.point(windowOrigin.x + p.x, windowOrigin.y + p.y);
    }
    onXChanged: place()
    onYChanged: place()
    onWidthChanged: place()
    onHeightChanged: place()
    onWindowOriginChanged: place()
    Component.onCompleted: { place(); Qt.callLater(place); }
    Connections {
        target: root.parent
        ignoreUnknownSignals: true
        function onXChanged() { root.place(); }
        function onYChanged() { root.place(); }
    }
    Connections {   // a panel inside a Loader that positions it (menus): follow the Loader too
        target: root.parent ? root.parent.parent : null
        ignoreUnknownSignals: true
        function onXChanged() { root.place(); }
        function onYChanged() { root.place(); }
    }

    // the wallpaper at screen size, cropped like the desktop draws it; rendered once into a small texture
    Item {
        id: wall
        width: root.screenSize.width
        height: root.screenSize.height
        visible: false
        Image {
            id: pic
            anchors.fill: parent
            source: root.source
            sourceSize: root.screenSize
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
            onStatusChanged: if (status === Image.Ready) texture.scheduleUpdate()
        }
    }
    ShaderEffectSource {
        id: texture
        sourceItem: wall
        hideSource: true
        live: false
        smooth: true
        // a smaller copy blurs for free when sampled; sharper glass keeps more detail
        textureSize: Qt.size(Math.max(64, root.screenSize.width / (1.5 + root.blur * 3)), Math.max(64, root.screenSize.height / (1.5 + root.blur * 3)))
        width: 1
        height: 1
        visible: false
    }

    ShaderEffect {
        anchors.fill: parent
        property var wall: texture
        property size size: Qt.size(root.width, root.height)
        property point origin: root.origin
        property size screenSize: root.screenSize
        property real radius: root.radius
        property real bezel: root.bezel
        property real refraction: root.refraction
        property real blur: root.blur
        property real rim: root.rim
        property color tint: root.tint
        fragmentShader: Quickshell.shellPath("assets/shaders/liquid_glass.frag.qsb")
    }
}
