import QtQuick
import Caelestia
import Caelestia.Config
import Caelestia.Multimedia
import Caelestia.Shaders
import Caelestia.State
import qs.services

// Aether: the wallpaper key pressed again while a transition plays speeds that transition to its end (~180 ms) and
// then plays the next one. The next picture starts loading the moment the key is pressed (`preload`), so the next
// transition starts at once - the old freeze was each new transition waiting for its picture to load. Pressed many
// times: the current one hurries once, and only the newest wallpaper plays next.
Item {
    id: root

    readonly property transitionShader shader: {
        CUtils.dependency(Config.background.wallpaperTransition);
        let transition = CUtils.enumToString(Config.background, "wallpaperTransition");
        if (!transition)
            return Shaders.crossfade;

        transition = transition[0].toLowerCase() + transition.slice(1);
        return Shaders[transition] ?? Shaders.crossfade;
    }

    property string source: Wallpapers.current
    property ShaderEffectSource currentCapture: capture1
    property real transitionProgress: 1
    property string pending: ""            // waits for the running transition to finish
    // also busy while a transition waits for its picture (progress 0, not running yet)
    readonly property bool busy: transitionAnim.running || hurryAnim.running || capturing || transitionProgress < 1
    property bool capturing: false

    // changes in quick succession play faster
    property real lastChange: 0
    property bool rapid: false

    onSourceChanged: {
        const now = Date.now();
        root.rapid = now - root.lastChange < 1500;
        root.lastChange = now;
        preload.source = root.source;      // decode it now
        if (root.busy) {
            root.pending = root.source;
            if (transitionAnim.running) {  // hurry the running one to its end
                transitionAnim.stop();
                hurryAnim.from = root.transitionProgress;
                hurryAnim.start();
            }
            return;
        }
        root.begin(root.source);
    }

    // capture the settled wallpaper on screen, then play the transition to `next`
    function begin(next: string): void {
        if (next === live.source && root.transitionProgress >= 1)
            return;
        root.capturing = true;
        root.target = next;
        currentCapture = currentCapture === capture1 ? capture2 : capture1;
        currentCapture.sourceItem = live;
        currentCapture.visible = true;
        currentCapture.scheduleUpdate();
    }
    property string target: ""

    function captured(cap: ShaderEffectSource): void {
        cap.visible = false;
        root.capturing = false;
        transition.from = cap;
        root.transitionProgress = 0;
        live.source = root.target;
        if (live.ready)
            transitionAnim.start();
    }

    // a transition is done: play what is waiting, if anything
    function next(): void {
        if (!root.pending)
            return;
        const p = root.pending;
        root.pending = "";
        Qt.callLater(() => root.begin(p));
    }

    Component.onCompleted: live.source = source

    NumberAnimation on transitionProgress {
        id: transitionAnim

        running: false
        duration: root.shader.duration * Tokens.anim.durations.scale * (root.rapid ? 0.45 : 1)
        easing: root.shader.easing
        to: 1
        onFinished: root.next()
    }

    // the rest of a transition, fast, because another wallpaper is waiting
    NumberAnimation {
        id: hurryAnim

        target: root
        property: "transitionProgress"
        to: 1
        duration: 180
        easing.type: Easing.OutCubic
        onFinished: root.next()
    }

    // the next picture, decoding while the current transition still plays (never shown itself)
    Visual {
        id: preload

        anchors.fill: parent
        visible: false
        playing: false
        animate: false
    }

    ShaderEffectSource {
        id: capture1

        anchors.fill: parent
        live: false
        visible: false
        onScheduledUpdateCompleted: root.captured(capture1)
    }

    ShaderEffectSource {
        id: capture2

        anchors.fill: parent
        live: false
        visible: false
        onScheduledUpdateCompleted: root.captured(capture2)
    }

    Visual {
        id: live

        anchors.fill: parent
        playing: PersistentState.wallpaperPlaying && !WinSettings.saving
        animate: false
        layer.enabled: true

        onReadyChanged: {
            if (ready && root.transitionProgress === 0 && !transitionAnim.running && !root.capturing)
                transitionAnim.start();
        }
    }

    ShaderEffect {
        id: transition

        property Item from
        property Item to: live
        property real progress: root.transitionProgress

        anchors.fill: parent
        visible: root.transitionProgress < 1
        fragmentShader: root.shader.url
    }
}
