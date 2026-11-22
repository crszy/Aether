// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick

// Units and money for the launcher and the toolbox: "10 km in mi", "72 f c", "100 usd pln", "1.5 gb mb".
// Units are worked out here; currencies use the European Central Bank's rates, which rice-tools fetches at most
// twice a day (ask() starts that, only when something that shows a conversion is open).
Singleton {
    id: cv
    property var rates: ({})            // code -> how many of it one euro buys
    property string ratesDate: ""
    property bool asked: false
    function ask() { if (!asked) { asked = true; rp.running = true } }
    DssProc {
        id: rp
        command: ["bash", "-c", "\"$HOME/.local/bin/rice-tools\" rates 2>/dev/null"]
        stdout: DssCollector { onStreamFinished: { try { const j = JSON.parse(text); cv.rates = j.rates || {}; cv.ratesDate = j.date || "" } catch (e) { } } }
    }
    // the rates are a day old at most while the shell runs
    Timer { interval: 6 * 3600 * 1000; repeat: true; running: cv.asked; onTriggered: rp.running = true }

    // group -> unit -> how many base units it is
    readonly property var units: ({
        length: { mm: 0.001, cm: 0.01, m: 1, km: 1000, "in": 0.0254, inch: 0.0254, ft: 0.3048, yd: 0.9144, mi: 1609.344, nmi: 1852 },
        mass: { mg: 1e-6, g: 0.001, dag: 0.01, kg: 1, t: 1000, oz: 0.028349523125, lb: 0.45359237, st: 6.35029318 },
        volume: { ml: 0.001, cl: 0.01, l: 1, m3: 1000, tsp: 0.00492892, tbsp: 0.0147868, cup: 0.2365882, floz: 0.0295735, pt: 0.473176, gal: 3.785411784 },
        speed: { "m/s": 1, "km/h": 1 / 3.6, kmh: 1 / 3.6, kph: 1 / 3.6, mph: 0.44704, kn: 0.514444 },
        time: { ms: 0.001, s: 1, sec: 1, min: 60, h: 3600, hr: 3600, d: 86400, day: 86400, wk: 604800, week: 604800 },
        data: { b: 1, kb: 1e3, mb: 1e6, gb: 1e9, tb: 1e12, kib: 1024, mib: 1048576, gib: 1073741824, tib: 1099511627776 },
        area: { cm2: 1e-4, m2: 1, km2: 1e6, ha: 1e4, ft2: 0.09290304, ac: 4046.8564224 },
        energy: { j: 1, kj: 1000, cal: 4.184, kcal: 4184, wh: 3600, kwh: 3.6e6 }
    })
    readonly property var temps: ["c", "f", "k"]
    readonly property var signs: ({ "$": "USD", "€": "EUR", "£": "GBP", "zł": "PLN", "zl": "PLN", "¥": "JPY" })
    // what the toolbox offers as chips, in a sensible order
    readonly property var groups: [
        { key: "length", sym: "straighten", list: ["mm", "cm", "m", "km", "in", "ft", "yd", "mi"] },
        { key: "mass", sym: "scale", list: ["g", "kg", "t", "oz", "lb"] },
        { key: "temperature", sym: "device_thermostat", list: ["c", "f", "k"] },
        { key: "volume", sym: "water_drop", list: ["ml", "l", "cup", "pt", "gal"] },
        { key: "speed", sym: "speed", list: ["m/s", "km/h", "mph", "kn"] },
        { key: "data", sym: "sd_card", list: ["kb", "mb", "gb", "tb", "mib", "gib"] },
        { key: "time", sym: "schedule", list: ["s", "min", "h", "d", "wk"] },
        { key: "money", sym: "payments", list: ["EUR", "USD", "PLN", "GBP", "CHF", "CZK", "JPY"] }
    ]

    function fmt(v) {
        if (!isFinite(v)) return ""
        const a = Math.abs(v)
        if (a !== 0 && (a < 1e-6 || a >= 1e15)) return v.toExponential(4)
        const s = a >= 100 ? v.toFixed(2) : v.toPrecision(6)
        return String(parseFloat(s))
    }
    function unit(u) {
        const l = u.toLowerCase().replace(/°/g, "").replace(/²/g, "2").replace(/³/g, "3")
        if (temps.indexOf(l) >= 0) return { g: "temperature", u: l }
        for (const g in units) if (units[g][l] !== undefined) return { g: g, u: l, k: units[g][l] }
        const code = signs[l] || u.toUpperCase()
        if (/^[A-Z]{3}$/.test(code) && (rates[code] !== undefined || code === "EUR")) return { g: "money", u: code, k: code === "EUR" ? 1 : rates[code] }
        return null
    }
    function looksLikeMoney(q) { return /\b[a-z]{3}\b/i.test(q) || /[$€£¥]|zł/.test(q) }
    // a number from one unit to another, or NaN when they do not go together
    function convert(v, from, to) {
        const a = unit(from), b = unit(to)
        if (!a || !b || a.g !== b.g) return NaN
        if (a.g === "temperature") {
            const c = a.u === "c" ? v : a.u === "f" ? (v - 32) * 5 / 9 : v - 273.15
            return b.u === "c" ? c : b.u === "f" ? c * 9 / 5 + 32 : c + 273.15
        }
        if (a.g === "money") return v / a.k * b.k
        return v * a.k / b.k
    }
    function label(u) { const x = unit(u); return !x ? u : x.g === "money" ? x.u : x.g === "temperature" ? (x.u === "k" ? "K" : "°" + x.u.toUpperCase()) : x.u }
    // "10 km in mi" -> { text: "6.21371 mi", val: "6.21371", from: "10 km" }, or null when it is not a conversion
    function parse(q) {
        const m = /^\s*(-?[\d\s]*[.,]?\d+)\s*([^\d\s]+?)\s*(?:\s(?:in|to|as|na|w|do)\s|\s*(?:=|->|→)\s*|\s+)([^\d\s]+)\s*$/i.exec(q)
        if (!m) return null
        const v = parseFloat(m[1].replace(/\s/g, "").replace(",", "."))
        if (isNaN(v)) return null
        const r = convert(v, m[2], m[3])
        if (isNaN(r)) return null
        const out = fmt(unit(m[3]).g === "money" ? Math.round(r * 100) / 100 : r)
        return { text: out + " " + label(m[3]), val: out, from: fmt(v) + " " + label(m[2]), money: unit(m[3]).g === "money" }
    }
}
