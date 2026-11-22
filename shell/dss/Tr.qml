// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick

// The language of the desktop shell and the lock screen: the one chosen in Settings > System > Languages
// (settings-app.json "lang"). Tr.t("English text") gives the text in that language, from lang/<code>.json next to this
// file, or the English text when there is no translation. Tr.loc: day and month names in that language.
Singleton {
    id: tr
    property string lang: "en"
    property var table: ({})
    readonly property var loc: Qt.locale(lang === "en" ? "en_US" : lang.replace("-", "_"))
    function t(s) { const v = table[s]; return v !== undefined ? v : s }
    function cap(s) { return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : s }
    readonly property var enDays: ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"]
    readonly property var enMonths: ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"]
    // 0 = Sunday
    function day(i) { return lang === "en" ? enDays[i] : cap(loc.standaloneDayName(i, Locale.LongFormat)) }
    // 0 = January; "of" = the form after a day number (Polish: 4 listopada)
    function month(i, of) { return lang === "en" ? enMonths[i] : (of ? loc.monthName(i, Locale.LongFormat) : cap(loc.standaloneMonthName(i, Locale.LongFormat))) }
    // the week from Monday: "narrow" (M T W ...) or two letters (Mo Tu ...)
    function week(narrow) {
        const o = []
        for (let k = 1; k <= 7; k++) {
            if (lang === "en") o.push(narrow ? "MTWTFSS"[k - 1] : ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"][k - 1])
            else o.push(cap(loc.standaloneDayName(k % 7, narrow ? Locale.NarrowFormat : Locale.ShortFormat)).slice(0, narrow ? 1 : 2))
        }
        return o
    }
    // the language itself (the lock screen has no shell.qml of the desktop to set it): settings-app.json "lang"
    readonly property string home: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/"))
    FileView {
        path: tr.home + "/.config/rice/settings-app.json"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            let j = {}
            try { j = JSON.parse(text()) } catch (e) { j = {} }
            tr.lang = (typeof j.lang === "string" && /^[a-zA-Z-]{2,6}$/.test(j.lang)) ? j.lang : "en"
        }
    }
    // the words: lang/<code>.json of the desktop shell (one place for the shell and the lock screen)
    FileView {
        path: tr.lang === "en" ? "" : Quickshell.shellDir + "/dss/lang/" + tr.lang + ".json"
        printErrors: false
        onLoaded: { try { tr.table = JSON.parse(text()) } catch (e) { tr.table = {} } }
        onLoadFailed: tr.table = {}
    }
    onLangChanged: if (lang === "en") table = {}
}
