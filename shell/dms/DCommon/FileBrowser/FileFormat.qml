pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import qs.dms.DCommon.Common

Singleton {
    function size(bytes) {
        if (bytes < 0)
            return "";
        const units = [I18n.tr("B", "file size unit"), I18n.tr("kB", "file size unit"), I18n.tr("MB", "file size unit"), I18n.tr("GB", "file size unit"), I18n.tr("TB", "file size unit")];
        let value = bytes;
        let unit = 0;
        while (value >= 1000 && unit < units.length - 1) {
            value /= 1000;
            unit++;
        }
        return `${value.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
    }

    function count(items) {
        if (items === 1)
            return I18n.tr("1 item", "a folder holding a single item");
        return I18n.tr("%1 items", "number of items in a folder").arg(items);
    }

    function modified(mtimeMs) {
        if (!mtimeMs)
            return "";
        return timestamp(new Date(mtimeMs), FileBrowserSettings.dateFormat, new Date());
    }

    function timestamp(date, format, now) {
        switch (format) {
        case "relative":
            return relativeTimestamp(date, now);
        case "iso":
            return Qt.formatDateTime(date, "yyyy-MM-dd HH:mm");
        case "isoSeconds":
            return Qt.formatDateTime(date, "yyyy-MM-dd HH:mm:ss");
        default:
            return date.toLocaleString(Qt.locale(), Locale.ShortFormat);
        }
    }

    function daysBefore(date, now) {
        const dayStart = d => new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime();
        return Math.round((dayStart(now) - dayStart(date)) / 86400000);
    }

    function relativeTimestamp(date, now) {
        const time = date.toLocaleTimeString(Qt.locale(), Locale.ShortFormat);
        const days = daysBefore(date, now);
        switch (true) {
        case days === 0:
            return I18n.tr("Today, %1", "relative file time, %1 is the time of day").arg(time);
        case days === 1:
            return I18n.tr("Yesterday, %1", "relative file time, %1 is the time of day").arg(time);
        case days > 1 && days < 7:
            return I18n.tr("%1, %2", "relative file time, %1 is the weekday name and %2 the time of day").arg(date.toLocaleDateString(Qt.locale(), "dddd")).arg(time);
        default:
            return date.toLocaleDateString(Qt.locale(), Locale.ShortFormat);
        }
    }

    function typeLabel(entry) {
        switch (true) {
        case entry.isDir:
            return I18n.tr("Folder", "file type shown for a directory");
        case entry.symlinkBroken:
            return I18n.tr("Broken link", "file type shown for a symlink with no target");
        case entry.extension !== "":
            return entry.extension.toUpperCase();
        case entry.mime !== "":
            return entry.mime;
        default:
            return I18n.tr("File", "file type shown when nothing more precise is known");
        }
    }

    function listingError(code) {
        switch (code) {
        case "ENOENT":
            return I18n.tr("This folder no longer exists", "directory listing error");
        case "EACCES":
            return I18n.tr("You do not have permission to view this folder", "directory listing error");
        case "UNAVAILABLE":
            return I18n.tr("Files are unavailable because the file service is not running", "file browser placeholder when no backend is connected");
        default:
            return I18n.tr("This folder could not be opened", "directory listing error");
        }
    }

    function operationError(code, fallback) {
        switch (code) {
        case "ENOENT":
            return I18n.tr("This item no longer exists", "file operation error");
        case "EACCES":
            return I18n.tr("You do not have permission to do this", "file operation error");
        case "EEXIST":
            return I18n.tr("This item already exists", "file conflict dialog header");
        case "EBUSY":
            return I18n.tr("This item is in use", "file operation error");
        case "EXDEV":
            return I18n.tr("This item cannot be moved here directly", "file operation error");
        case "NOTSUPPORTED":
        case "":
            return fallback || I18n.tr("This item could not be handled", "file operation error");
        default:
            return I18n.tr("This item could not be handled", "file operation error");
        }
    }

    function location(path) {
        const parent = FilePaths.parentOf(path);
        if (parent === "")
            return "";
        const home = FilePaths.home;
        if (home !== "" && (parent === home || parent.startsWith(home + "/")))
            return "~" + parent.substring(home.length);
        return parent;
    }

    function escapeMarkup(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    function highlight(text, needle, color) {
        if (needle === "")
            return text;
        const at = text.toLowerCase().indexOf(needle.toLowerCase());
        if (at < 0)
            return escapeMarkup(text);
        return markRanges(text, [[at, at + needle.length]], color);
    }

    function markRanges(text, marks, color) {
        let out = "";
        let at = 0;
        for (const mark of marks) {
            out += escapeMarkup(text.substring(at, mark[0])) + `<font color="${color}"><b>` + escapeMarkup(text.substring(mark[0], mark[1])) + "</b></font>";
            at = mark[1];
        }
        return out + escapeMarkup(text.substring(at));
    }

    function stemLength(name, isDir) {
        if (isDir)
            return name.length;
        const compound = name.search(/\.tar\.[^.]+$/i);
        if (compound > 0)
            return compound;
        const dot = name.lastIndexOf(".");
        return dot > 0 ? dot : name.length;
    }

    function shownName(displayName, name, isDir) {
        if (displayName !== "")
            return displayName;
        if (FileBrowserSettings.showExtensions)
            return name;
        return name.substring(0, stemLength(name, isDir));
    }

    function validName(name) {
        const trimmed = name.trim();
        return trimmed !== "" && trimmed !== "." && trimmed !== ".." && !trimmed.includes("/");
    }
}
