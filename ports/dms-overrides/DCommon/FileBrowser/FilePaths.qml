pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import QtCore
import Quickshell

// Windows version of DMS's FilePaths. Upstream assumes Linux paths ("/home/..."): "C:/x" became the URL
// "file:///C%3A/x", which loads nothing (blank thumbnails and previews), and "C:/" had no root. Here drive
// letters are kept, "\" is read as "/", and the drive root ("C:/") is a root like "/" is on Linux.
Singleton {
    readonly property string home: fromUrl(StandardPaths.writableLocation(StandardPaths.HomeLocation))

    function _drive(path) {
        const m = /^([A-Za-z]:)(\/|$)/.exec(path);
        return m ? m[1].toUpperCase() : "";
    }

    function toFileUrl(path) {
        if (!path)
            return "";
        const text = String(path);
        if (/^(file|qrc|https?|image):/i.test(text))
            return text;
        const p = text.replace(/\\/g, "/").replace(/^\/(?=[A-Za-z]:)/, "");
        const parts = p.split("/").map((segment, i) => (i === 0 && /^[A-Za-z]:$/.test(segment)) ? segment : encodeURIComponent(segment));
        return "file:///" + parts.join("/").replace(/^\/+/, "");
    }

    function fromUrl(value) {
        let text = String(value ?? "");
        if (!/^file:/i.test(text))
            return text.replace(/\\/g, "/");
        text = text.replace(/^file:\/*/i, "");
        try {
            text = decodeURIComponent(text);
        } catch (e) {}
        // file:///C:/x -> C:/x ; file:///home/x (Linux style) -> /home/x
        return /^[A-Za-z]:/.test(text) ? text : "/" + text;
    }

    function expandTilde(path) {
        if (path === "~")
            return home;
        if (path.startsWith("~/"))
            return home + path.substring(1);
        return path;
    }

    function parentOf(path) {
        if (path === "" || path === "/")
            return "";
        const p = String(path).replace(/\\/g, "/").replace(/\/+$/, "");
        if (/^[A-Za-z]:$/.test(p))
            return ""; // a drive root has no parent
        const cut = p.lastIndexOf("/");
        if (cut < 0)
            return "";
        const parent = p.substring(0, cut);
        if (/^[A-Za-z]:$/.test(parent))
            return parent + "/";
        return cut === 0 ? "/" : parent;
    }

    function baseName(path) {
        const p = String(path).replace(/\\/g, "/").replace(/\/+$/, "");
        if (/^[A-Za-z]:$/.test(p))
            return p;
        return p.substring(p.lastIndexOf("/") + 1);
    }

    function join(directory, name) {
        const d = String(directory).replace(/\\/g, "/");
        return d.endsWith("/") ? d + name : d + "/" + name;
    }

    function normalize(path) {
        let absolute = expandTilde(fromUrl(String(path ?? "")).trim()).replace(/\\/g, "/");
        // "/" alone or "/Users/..." without a drive: the system drive
        if (absolute.startsWith("/") && !/^\/[A-Za-z]:/.test(absolute))
            absolute = "C:" + absolute;
        absolute = absolute.replace(/^\/(?=[A-Za-z]:)/, "");
        const drive = _drive(absolute);
        if (!drive)
            return "";
        const parts = [];
        for (const part of absolute.substring(2).split("/")) {
            switch (part) {
            case "":
            case ".":
                break;
            case "..":
                parts.pop();
                break;
            default:
                parts.push(part);
            }
        }
        return drive + "/" + parts.join("/");
    }
}
