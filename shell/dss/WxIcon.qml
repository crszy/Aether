// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// A weather icon from wttr.in's words ("Light rain", "Partly cloudy" ...): Material Symbols, or a Nerd Font glyph.
MIcon {
    property string cond: ""
    property bool night: false
    readonly property string s: cond.toLowerCase()
    readonly property int kind: s.indexOf("thunder") >= 0 ? 5
        : (s.indexOf("snow") >= 0 || s.indexOf("sleet") >= 0 || s.indexOf("ice") >= 0 || s.indexOf("blizzard") >= 0) ? 4
        : (s.indexOf("rain") >= 0 || s.indexOf("drizzle") >= 0 || s.indexOf("shower") >= 0) ? 3
        : (s.indexOf("fog") >= 0 || s.indexOf("mist") >= 0 || s.indexOf("haze") >= 0) ? 6
        : (s.indexOf("partly") >= 0) ? 2
        : (s.indexOf("cloud") >= 0 || s.indexOf("overcast") >= 0) ? 1 : 0
    name: ["clear_day", "cloud", "partly_cloudy_day", "rainy", "weather_snowy", "thunderstorm", "foggy"][kind]
        .replace("clear_day", night ? "bedtime" : "clear_day").replace("partly_cloudy_day", night ? "partly_cloudy_night" : "partly_cloudy_day")
    glyph: String.fromCodePoint([night ? 0xF0594 : 0xF0599, 0xF0590, night ? 0xF0F31 : 0xF0595, 0xF0597, 0xF0598, 0xF0593, 0xF0591][kind])
}
