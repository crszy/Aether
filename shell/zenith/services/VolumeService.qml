// services/VolumeService.qml - Windows version
// Zenith's original drives wpctl / pw-mon / python helper scripts. This one has the same properties and
// functions, backed by Quickshell.Services.Pipewire, which Caelestia for Windows emulates on top of the
// Windows audio API (the same source Caelestia's own volume controls use). Changes arrive as events,
// so nothing polls.
import QtQuick
import Quickshell
import Quickshell.Services.Pipewire
import "../Settings"
pragma Singleton

Item {
    id: service

    readonly property PwNode _sink: Pipewire.defaultAudioSink
    readonly property PwNode _source: Pipewire.defaultAudioSource

    readonly property int outputVolume: _sink?.audio ? Math.round(_sink.audio.volume * 100) : 0
    readonly property int micVolume: _source?.audio ? Math.round(_source.audio.volume * 100) : 0
    readonly property bool muted: _sink?.audio?.muted ?? false
    readonly property bool micMuted: _source?.audio?.muted ?? false
    readonly property bool micActive: false
    readonly property bool btActive: /bluetooth|hands-free|headset|airpods|buds/i.test(_sink?.description ?? "")
    property var sinks: []
    property var sources: []
    readonly property int activeSinkId: _sink ? _sink.id : -1
    readonly property int activeSourceId: _source ? _source.id : -1

    readonly property alias appsModel: appModel

    function update() {
        updateTimer.restart();
    }

    function _node(id) {
        for (const n of Pipewire.nodes.values)
            if (n.id === Number(id))
                return n;
        return null;
    }

    function setDefaultDevice(devId) {
        const n = _node(devId);
        if (!n)
            return;
        if (n.isSink)
            Pipewire.preferredDefaultAudioSink = n;
        else
            Pipewire.preferredDefaultAudioSource = n;
        update();
    }

    function toggleMute() {
        if (_sink?.audio)
            _sink.audio.muted = !_sink.audio.muted;
    }

    function setOutputVolume(val) {
        if (!_sink?.audio)
            return;
        _sink.audio.muted = false;
        _sink.audio.volume = Math.max(0, Math.min(150, val)) / 100;
    }

    function toggleMicMute() {
        if (_source?.audio)
            _source.audio.muted = !_source.audio.muted;
    }

    function setMicVolume(val) {
        if (!_source?.audio)
            return;
        _source.audio.muted = false;
        _source.audio.volume = Math.max(0, Math.min(150, val)) / 100;
    }

    // per-application volume (the Windows build's VolumeContent calls this instead of wpctl)
    function setAppVolume(appId, val) {
        const n = _node(appId);
        if (n?.audio) {
            n.audio.muted = false;
            n.audio.volume = Math.max(0, Math.min(150, val)) / 100;
        }
    }

    function _performUpdate() {
        const sk = [], sr = [];
        const streams = [];
        for (const n of Pipewire.nodes.values) {
            if (!n.audio)
                continue;
            if (n.isStream) {
                if (n.isSink)
                    streams.push(n);
                continue;
            }
            const entry = { id: n.id, name: n.description || n.nickname || n.name, isDefault: false };
            if (n.isSink) {
                entry.isDefault = n === _sink;
                sk.push(entry);
            } else {
                entry.isDefault = n === _source;
                sr.push(entry);
            }
        }
        sinks = sk;
        sources = sr;
        _syncApps(streams);
    }

    function _syncApps(streams) {
        const ids = new Set();
        for (const s of streams) {
            ids.add(s.id);
            const vol = Math.round(s.audio.volume * 100);
            const name = String(s.properties?.["application.name"] || s.description || s.name || "Unknown App");
            let found = false;
            for (let j = 0; j < appModel.count; j++) {
                const item = appModel.get(j);
                if (item.appId === s.id) {
                    if (item.volume !== vol)
                        appModel.setProperty(j, "volume", vol);
                    if (item.muted !== s.audio.muted)
                        appModel.setProperty(j, "muted", s.audio.muted);
                    found = true;
                    break;
                }
            }
            if (!found)
                appModel.append({ appId: s.id, name: name, volume: vol, muted: s.audio.muted, icon: "" });
        }
        for (let j = appModel.count - 1; j >= 0; j--)
            if (!ids.has(appModel.get(j).appId))
                appModel.remove(j);
    }

    Timer {
        id: updateTimer
        interval: 150
        onTriggered: service._performUpdate()
    }

    // keeps every node's volume/mute live (Pipewire only fills .audio for tracked nodes)
    PwObjectTracker {
        objects: Pipewire.nodes.values
    }

    Connections {
        target: Pipewire.nodes
        function onValuesChanged() { service.update(); }
    }

    Connections {
        target: Pipewire
        function onDefaultAudioSinkChanged() { service.update(); }
        function onDefaultAudioSourceChanged() { service.update(); }
    }

    // stream volumes change without the node list changing: refresh the app list while a menu shows it
    Timer {
        interval: 1000
        repeat: true
        running: Variables.activeMenuOpen
        onTriggered: service._performUpdate()
    }

    Component.onCompleted: update()

    ListModel {
        id: appModel
    }
}
