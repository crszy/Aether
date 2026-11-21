import QtQuick
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.common.widgets

QuickToggleModel {
    toggled: SongRec.running
    property bool sourceIsMonitor: SongRec.monitorSource === SongRec.MonitorSource.Monitor

    name: Translation.tr("Identify Music")
    statusText: toggled ? Translation.tr("Listening...") : sourceIsMonitor ? Translation.tr("System sound") : Translation.tr("Microphone")
    icon: toggled ? "music_cast" : (sourceIsMonitor ? "music_note" : "frame_person_mic")

    tooltipText: Translation.tr("Recognize music | Right-click to toggle source")

    mainAction: () => {
        SongRec.toggleRunning()
    }
    altAction: () => {
        SongRec.toggleMonitorSource()
    }
}
