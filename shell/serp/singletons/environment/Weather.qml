// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later); the Open-Meteo fetch below is weather.sh in QML.
pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io
import "../../"

Item {
    id: root

    property int refreshInterval: {
        let gs = (typeof Config !== "undefined" && typeof Config.getSetting === "function")
            ? Config.getSetting("general", {})
            : ((typeof Config !== "undefined" && Config.rawSettings) ? Config.rawSettings.general : null);
        let mins = (gs && gs.weatherInterval !== undefined) ? Number(gs.weatherInterval) : 30;
        return (!isNaN(mins) && mins > 0 ? mins : 30) * 60000;
    }
    property string unit: {
        let gs = (typeof Config !== "undefined" && typeof Config.getSetting === "function")
            ? Config.getSetting("general", {})
            : ((typeof Config !== "undefined" && Config.rawSettings) ? Config.rawSettings.general : null);
        return (gs && gs.weatherUnit !== undefined) ? gs.weatherUnit : "metric";
    }
    property var customLocation: null

    readonly property var activeLocation: customLocation !== null 
        ? customLocation 
        : ((typeof Location !== "undefined" && Location.locationData && Object.keys(Location.locationData).length > 0) 
            ? Location.locationData 
            : ((typeof Config !== "undefined" && Config.rawSettings && Config.rawSettings.general && Config.rawSettings.general.location) 
                ? Config.rawSettings.general.location 
                : null))

    property var data: ({})
    property var forecast: []
    property string currentIcon: ""
    property int currentCode: -1
    property int currentIsDay: 1
    property string currentTemp: ""
    property string currentTempFormatted: "--°"
    property string currentHex: (typeof ThemeBackend !== "undefined" && ThemeBackend.yellow) ? ThemeBackend.yellow.toString() : "#f9e2af"
    property string unitSym: "°C"
    property real latitude: 0.0
    property real longitude: 0.0

    property bool isLoading: false
    property bool isReady: false
    property bool _forceFetchMode: false

    property string _lastLocationFingerprint: ""
    property string _lastUnit: ""
    property string _lastRaw: ""

    signal weatherUpdated()

    onActiveLocationChanged: {
        checkLocationUpdate();
    }

    onUnitChanged: {
        if (root._lastUnit !== "" && root._lastUnit !== root.unit) {
            root._lastUnit = root.unit;
            root.refresh(true);
        } else if (root._lastUnit === "") {
            root._lastUnit = root.unit;
        }
    }

    Connections {
        target: typeof Location !== "undefined" ? Location : null
        function onLocationUpdated() {
            root.checkLocationUpdate();
        }
    }

    Connections {
        target: typeof Config !== "undefined" ? Config : null
        function onSettingsLoaded() {
            root.checkLocationUpdate();
            let gs = (typeof Config !== "undefined" && typeof Config.getSetting === "function")
                ? Config.getSetting("general", {})
                : ((typeof Config !== "undefined" && Config.rawSettings) ? Config.rawSettings.general : null);
            if (gs) {
                if (gs.weatherInterval !== undefined) {
                    let mins = Number(gs.weatherInterval);
                    if (!isNaN(mins) && mins > 0) {
                        root.refreshInterval = mins * 60000;
                    }
                }
                if (gs.weatherUnit !== undefined && gs.weatherUnit !== root.unit) {
                    root.unit = gs.weatherUnit;
                }
            }
            root.refresh(false);
        }
    }

    function checkLocationUpdate() {
        let fp = JSON.stringify(root.activeLocation || {});
        if (root._lastLocationFingerprint !== "" && root._lastLocationFingerprint !== fp) {
            root._lastLocationFingerprint = fp;
            root.refresh(true);
        } else if (root._lastLocationFingerprint === "") {
            root._lastLocationFingerprint = fp;
        }
    }

    function refresh(showLoader) {
        let shouldShowLoader = showLoader !== undefined ? !!showLoader : true;
        root._forceFetchMode = shouldShowLoader;
        if (shouldShowLoader) {
            root.isLoading = true;
        }
        fetchProcess.running = false;
        fetchProcess.running = true;
    }

    function forceFetch() {
        refresh(true);
        return root.data;
    }

    function getData() {
        return root.data;
    }

    function parseJson(rawText) {
        if (!rawText) {
            root.isLoading = false;
            return;
        }
        let txt = typeof rawText === "function" ? rawText() : rawText;
        if (typeof txt !== "string") txt = String(txt || "");
        txt = txt.trim();
        if (txt === "") {
            root.isLoading = false;
            return;
        }

        if (txt === root._lastRaw) {
            root.isLoading = false;
            return;
        }

        try {
            let parsed = JSON.parse(txt);
            root._lastRaw = txt;
            root.data = parsed;

            if (parsed.offline === true) {
                root.currentTemp = "";
                root.currentTempFormatted = "--°";
            }

            if (parsed.current_icon !== undefined) {
                root.currentIcon = parsed.current_icon;
            }
            root.currentCode = parsed.current_code !== undefined && parsed.current_code !== null ? Number(parsed.current_code) : -1;
            root.currentIsDay = parsed.current_is_day === 0 ? 0 : 1;
            if (parsed.current_temp_formatted !== undefined) {
                root.currentTempFormatted = parsed.current_temp_formatted;
            } else if (parsed.current_temp !== undefined && parsed.current_temp !== null) {
                let sym = parsed.unit_sym || "°C";
                root.currentTempFormatted = parsed.current_temp.toString() + sym;
            }
            if (parsed.current_temp !== undefined) {
                root.currentTemp = parsed.current_temp.toString();
            }
            if (parsed.current_hex !== undefined) {
                root.currentHex = parsed.current_hex;
            }
            if (parsed.unit_sym !== undefined) {
                root.unitSym = parsed.unit_sym;
            }
            if (parsed.latitude !== undefined) {
                root.latitude = Number(parsed.latitude);
            }
            if (parsed.longitude !== undefined) {
                root.longitude = Number(parsed.longitude);
            }
            if (parsed.forecast && Array.isArray(parsed.forecast)) {
                root.forecast = parsed.forecast;
            }

            root.isReady = true;
            root.isLoading = false;
            root.weatherUpdated();
        } catch(e) {
            root.isLoading = false;
        }
    }

    // Windows: weather.sh (bash, curl, jq) is replaced by the same Open-Meteo call made here, giving the same JSON
    // (current_*, forecast[5] with hourly slots every 3 h). No location set: the offline card, as on Linux.
    readonly property var fetchProcess: QtObject {
        property bool running: false
        onRunningChanged: if (running) root.fetchNative()
    }

    function wmoMap(code, isDay) {
        if (code === 0)
            return isDay === 1 ? { icon: "", hex: "#f9e2af", desc: "Sunny" } : { icon: "", hex: "#cba6f7", desc: "Clear" };
        if (code >= 1 && code <= 3)
            return { icon: "", hex: "#bac2de", desc: "Cloudy" };
        if (code === 45 || code === 48)
            return { icon: "󰖑", hex: "#84afdb", desc: "Mist" };
        if ((code >= 51 && code <= 57) || (code >= 61 && code <= 67) || (code >= 80 && code <= 82))
            return { icon: "󰖗", hex: "#74c7ec", desc: "Rainy" };
        if ((code >= 71 && code <= 77) || code === 85 || code === 86)
            return { icon: "", hex: "#cdd6f4", desc: "Snow" };
        if (code === 95 || code === 96 || code === 99)
            return { icon: "", hex: "#f9e2af", desc: "Storm" };
        return { icon: "", hex: "#cdd6f4", desc: "Unknown" };
    }

    function fetchNative() {
        const loc = root.activeLocation || {};
        const unit = root.unit || "metric";
        const sym = unit === "imperial" ? "°F" : unit === "standard" ? "K" : "°C";
        const kelvin = unit === "standard";
        const done = obj => {
            root.parseJson(JSON.stringify(obj));
            root.isLoading = false;
            root._forceFetchMode = false;
            fetchProcess.running = false;
        };
        const offline = () => done({ latitude: 0, longitude: 0, offline: true, unit: unit, unit_sym: sym, current_icon: "", current_hex: "#cdd6f4", forecast: root.forecast || [] });
        const lat = Number(loc.latitude), lon = Number(loc.longitude);
        if (loc.latitude === undefined || loc.longitude === undefined || isNaN(lat) || isNaN(lon)) {
            offline();
            return;
        }
        const tz = loc.timezone || "auto";
        const url = "https://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon + "&timezone=" + encodeURIComponent(tz)
            + "&current=temperature_2m,weather_code,is_day&hourly=temperature_2m,apparent_temperature,precipitation_probability,weather_code,relative_humidity_2m,wind_speed_10m"
            + "&daily=sunrise,sunset&past_days=1" + (unit === "imperial" ? "&temperature_unit=fahrenheit&wind_speed_unit=mph" : "");
        const fmt = t => {
            if (t === null || t === undefined)
                return "0.0";
            let v = Math.round((kelvin ? t + 273.15 : t) * 10) / 10;
            if (v === 0)
                v = 0;
            const s = String(v);
            return s.indexOf(".") >= 0 ? s : s + ".0";
        };
        const xhr = new XMLHttpRequest();
        xhr.onreadystatechange = () => {
            if (xhr.readyState !== XMLHttpRequest.DONE)
                return;
            let r;
            try {
                r = JSON.parse(xhr.responseText);
            } catch (e) {
                offline();
                return;
            }
            if (!r || r.error || !r.current || !r.hourly) {
                offline();
                return;
            }
            const today = String(r.current.time).split("T")[0];
            const days = [];
            const byDay = {};
            for (let i = 0; i < r.hourly.time.length; i++) {
                const d = String(r.hourly.time[i]).split("T")[0];
                if (d < today)
                    continue;
                if (!byDay[d]) {
                    byDay[d] = [];
                    days.push(d);
                }
                byDay[d].push({ time_raw: r.hourly.time[i], temp: r.hourly.temperature_2m[i], feels_like: r.hourly.apparent_temperature[i],
                    pop: r.hourly.precipitation_probability[i], humidity: r.hourly.relative_humidity_2m[i], wind: r.hourly.wind_speed_10m[i], code: r.hourly.weather_code[i] });
            }
            const daily = r.daily || {};
            const forecast = days.slice(0, 5).map((key, idx) => {
                const hrs = byDay[key];
                const p = key.split("-").map(Number);
                const dt = new Date(p[0], p[1] - 1, p[2]);
                const nums = f => hrs.map(h => h[f]).filter(v => v !== null && v !== undefined);
                const dIdx = (daily.time || []).indexOf(key);
                const tpart = s => s ? String(s).split("T")[1] : null;
                const code = (hrs[12] || hrs[0]).code;
                const m = root.wmoMap(code, 1);
                const hum = nums("humidity");
                return {
                    id: String(idx),
                    day: Qt.formatDate(dt, "ddd").toLowerCase(),
                    day_full: Qt.formatDate(dt, "dddd").toLowerCase(),
                    date: Qt.formatDate(dt, "dd") + " " + Qt.formatDate(dt, "MMM").toLowerCase(),
                    max: fmt(Math.max.apply(null, nums("temp"))),
                    min: fmt(Math.min.apply(null, nums("temp"))),
                    feels_like: fmt(Math.max.apply(null, nums("feels_like"))),
                    wind: String(Math.round(Math.max.apply(null, nums("wind")))),
                    humidity: String(hum.length ? Math.round(hum.reduce((a, b) => a + b, 0) / hum.length) : 0),
                    pop: String(Math.max.apply(null, nums("pop").concat([0]))),
                    sunrise: dIdx < 0 ? null : tpart(daily.sunrise[dIdx]),
                    sunset: dIdx < 0 ? null : tpart(daily.sunset[dIdx]),
                    icon: m.icon,
                    hex: m.hex,
                    code: code,
                    desc: m.desc,
                    hourly: hrs.filter((h, i) => i % 3 === 0).map(h => {
                        const t = String(h.time_raw).split("T")[1];
                        const hour = Number(t.split(":")[0]);
                        const day = hour >= 6 && hour <= 18 ? 1 : 0;
                        const sm = root.wmoMap(h.code, day);
                        return { time: t, pop: h.pop || 0, temp: fmt(h.temp), icon: sm.icon, hex: sm.hex, code: h.code, is_day: day };
                    })
                };
            });
            const cm = root.wmoMap(r.current.weather_code, r.current.is_day);
            done({
                latitude: lat, longitude: lon, location_updated_at: Number(loc.updated_at || 0), fetched_at: Math.floor(Date.now() / 1000),
                lang: "en", unit: unit, unit_sym: sym,
                current_temp: fmt(r.current.temperature_2m), current_temp_formatted: fmt(r.current.temperature_2m) + sym,
                current_icon: cm.icon, current_hex: cm.hex, current_code: r.current.weather_code, current_is_day: r.current.is_day,
                forecast: forecast
            });
        };
        xhr.open("GET", url);
        xhr.send();
    }

    FileView {
        id: weatherWatcher
        path: Caching.cacheDir ? (Caching.getCacheDir("weather") + "/weather.json") : ""
        watchChanges: true
        onLoaded: root.parseJson(typeof weatherWatcher.text === "function" ? weatherWatcher.text() : weatherWatcher.text)
        onFileChanged: root.parseJson(typeof weatherWatcher.text === "function" ? weatherWatcher.text() : weatherWatcher.text)
    }

    Timer {
        id: refreshTimer
        interval: root.refreshInterval
        running: true
        repeat: true
        triggeredOnStart: false
        onTriggered: root.refresh(false)
    }

    Component.onCompleted: {
        root._lastLocationFingerprint = JSON.stringify(root.activeLocation || {});
        root._lastUnit = root.unit;
        if (typeof Config !== "undefined" && Config.dataReady) {
            root.refresh(false);
        }
    }
}
