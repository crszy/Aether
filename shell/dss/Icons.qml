pragma Singleton
// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows version by dotfiles/dss-port.js: same API (get, src, one, files, rev), but the names are looked up by
// Quickshell.iconPath (Windows' own app icons) instead of iconfile.sh walking Linux icon themes.
import Quickshell
import QtQuick

Singleton {
    id: ic
    property var files: ({})
    property int rev: 0

    function get(n, fallback) {
        const r = ic.rev
        const p = one(n)
        if (p !== "") return p
        if (typeof fallback === "string" && fallback !== "") return one(fallback)
        return ""
    }
    function src(u) {
        if (!u) return ""
        u = String(u)
        if (u.indexOf("image://icon/") !== 0) return u
        let n = u.substring(13)
        const q = n.indexOf("?")
        if (q >= 0) n = n.substring(0, q)
        try { n = decodeURIComponent(n) } catch (e) {}
        const p = one(n)
        return p !== "" ? p : u
    }
    function one(n) {
        if (!n) return ""
        n = String(n)
        if (n.indexOf("file://") === 0) return n
        if (n.indexOf("://") > 0) return ""
        if (n.indexOf("|") > 0) n = n.substring(0, n.indexOf("|"))
        const f = ic.files[n]
        if (f !== undefined) return f
        let p = ""
        try { p = String(Quickshell.iconPath(n, true) || "") } catch (e) { p = "" }
        ic.files[n] = p
        return p
    }
}
