// services/WeatherService.qml
import QtQuick
import Quickshell
import Quickshell.Io
import "../Settings"

pragma Singleton

Item {
    id: service

    property var weatherData: null
    property bool loading: true

    readonly property string tempC: weatherData?.current_condition?.[0]?.temp_C || "0"
    readonly property string weatherCode: weatherData?.current_condition?.[0]?.weatherCode || "113"
    readonly property string weatherDesc: weatherData?.current_condition?.[0]?.weatherDesc?.[0]?.value || ""
    readonly property string areaName: weatherData?.nearest_area?.[0]?.areaName?.[0]?.value || "Unknown"

    readonly property string scriptPath: PathSettings.scriptsDir + "/weather.sh"

    function getIcon(code) {
        const c = parseInt(code);
        if (c === 113) return ""; 
        if (c === 116) return ""; 
        if (c === 119 || c === 122) return "";
        if ([143, 248, 260].includes(c)) return ""; 
        if ([176, 263, 266, 293, 296, 302, 308].includes(c)) return "";
        if ([200, 386, 389].includes(c)) return ""; 
        return "";
    }

    function refresh() {
        if (!weatherProc.running) {
            service.loading = true;
            weatherProc.running = true;
        }
    }

    // weather.sh keeps its own on-disk cache and answers from it instantly
    // when it is under 30 minutes old, so the first call paints the bar at
    // once; only a stale cache costs a network round-trip.
    Component.onCompleted: refresh()

    Process {
        id: weatherProc
        command: ["curl.exe", "-s", "-m", "10", "https://wttr.in/?format=j1"] // Windows
        stdout: StdioCollector {
            onStreamFinished: {
                service.loading = false;
                if (!text || text.trim() === "") return;
                try {
                    let parsed = JSON.parse(text);
                    if (parsed && parsed.current_condition) {
                        service.weatherData = parsed;
                    }
                } catch (e) {}
            }
        }
    }

    Timer {
        interval: 1800000 // 30 minutes
        running: true
        repeat: true
        onTriggered: service.refresh()
    }
}
