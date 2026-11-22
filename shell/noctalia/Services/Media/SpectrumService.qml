pragma Singleton

// Caelestia for Windows: Noctalia's SpectrumService on Quickshell.Windows' AudioVisualizer (WASAPI loopback)
// instead of PipeWire's PwAudioSpectrum. Same API: registerComponent / unregisterComponent / values / isIdle.
import QtQuick
import Quickshell
import Quickshell.Windows as Win
import qs.noctalia.Commons

Singleton {
  id: root

  function registerComponent(componentId) {
    root._registeredComponents[componentId] = true;
    root._registeredComponents = Object.assign({}, root._registeredComponents);
  }

  function unregisterComponent(componentId) {
    delete root._registeredComponents[componentId];
    root._registeredComponents = Object.assign({}, root._registeredComponents);
  }

  function isRegistered(componentId) {
    return root._registeredComponents[componentId] === true;
  }

  property var _registeredComponents: ({})
  readonly property int _registeredCount: Object.keys(_registeredComponents).length
  property bool _shouldRun: _registeredCount > 0

  // 0..1 per band, like PwAudioSpectrum
  readonly property var values: {
    if (!_shouldRun)
      return [];
    const max = Math.max(1, Win.AudioVisualizer.maxValue);
    return Win.AudioVisualizer.values.map(v => Math.max(0, Math.min(1, v / max)));
  }
  readonly property bool isIdle: !_shouldRun || !Win.AudioVisualizer.capturing

  // the visualizer is shared with Caelestia: only switch it on here, never off
  on_ShouldRunChanged: if (_shouldRun) {
    Win.AudioVisualizer.bars = Settings.data.audio.spectrumMirrored ? 32 : 64;
    Win.AudioVisualizer.running = true;
  }
}
