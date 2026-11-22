pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as Win

// Windows version of DMS's FilesService. Upstream every call goes to DMS's Go backend ("dms" binary, Linux
// only) - here the same calls are answered from Windows itself (Quickshell.Windows FsUtils / ImageTools), with
// the same result shapes, so DMS's file browser, file picker and their previews work. "Trash" is the
// Recycle Bin (nothing is deleted for good).
Singleton {
    id: root

    readonly property bool connected: true
    readonly property var capabilities: ({
            "mkdir": true,
            "rename": true,
            "trash": true
        })
    readonly property string home: (Quickshell.env("USERPROFILE") || "C:/Users/Public").replace(/\\/g, "/")
    property var userDirs: []
    property int refCount: 0

    signal watchEvent(var data)
    signal searchEvent(var data)

    function addRef() {
        refCount++;
    }
    function removeRef() {
        refCount = Math.max(0, refCount - 1);
    }
    function ensureSubscription() {}

    function refreshUserDirs() {
        const dirs = [
            { key: "home", name: "Home", path: home, iconName: "user-home" },
            { key: "desktop", name: "Desktop", path: `${home}/Desktop`, iconName: "user-desktop" },
            { key: "documents", name: "Documents", path: `${home}/Documents`, iconName: "folder-documents" },
            { key: "downloads", name: "Downloads", path: `${home}/Downloads`, iconName: "folder-download" },
            { key: "pictures", name: "Pictures", path: `${home}/Pictures`, iconName: "folder-pictures" },
            { key: "music", name: "Music", path: `${home}/Music`, iconName: "folder-music" },
            { key: "videos", name: "Videos", path: `${home}/Videos`, iconName: "folder-videos" }
        ].filter(d => Win.FsUtils.classify(d.path) === "dir");
        // every drive
        for (const letter of "CDEFGHIJKLMNOPQRSTUVWXYZ") {
            const p = `${letter}:/`;
            if (Win.FsUtils.classify(p) === "dir")
                dirs.push({ key: `drive-${letter}`, name: `${letter}:`, path: p, iconName: "drive-harddisk" });
        }
        userDirs = dirs;
    }
    Component.onCompleted: refreshUserDirs()

    // ---- helpers
    function _norm(p) {
        let s = String(p || "").replace(/\\/g, "/");
        if (s === "~" || s.startsWith("~/"))
            s = home + s.slice(1);
        if (s === "/")
            return "C:/";
        if (/^[A-Za-z]:$/.test(s))
            s += "/";
        return s;
    }
    function _glob(pattern) {
        const esc = pattern.replace(/[.+^${}()|[\]\\]/g, "\\$&").replace(/\*/g, ".*").replace(/\?/g, ".");
        return new RegExp(`^${esc}$`, "i");
    }
    // every field DMS's file tiles / rows require (a missing one and the tile is not made at all)
    function _complete(e) {
        if (!e || !e.path)
            return e;
        const dot = e.isDir ? -1 : e.name.lastIndexOf(".");
        const mime = String(e.mime || "application/octet-stream");
        return {
            name: e.name,
            displayName: e.name,
            path: e.path,
            dirPath: e.path.replace(/\/[^/]*$/, "") || e.path,
            isDir: e.isDir === true,
            isSymlink: e.isLink === true,
            symlinkBroken: false,
            hidden: e.hidden === true || e.name.startsWith("."),
            size: Number(e.size) || 0,
            mtimeMs: Number(e.mtimeMs) || 0,
            ctimeMs: Number(e.mtimeMs) || 0,
            mime: mime,
            extension: dot > 0 ? e.name.slice(dot + 1).toLowerCase() : "",
            iconName: e.isDir ? "folder" : mime.startsWith("image/") ? "image-x-generic" : mime.startsWith("video/") ? "video-x-generic" : mime.startsWith("audio/") ? "audio-x-generic" : mime.startsWith("text/") ? "text-x-generic" : mime.replace("/", "-"),
            thumbnail: "",
            thumbnailable: e.thumbnailable === true,
            mode: "",
            owner: "",
            untrusted: false,
            unreadable: false,
            depth: 0
        };
    }

    function _entries(path, opts) {
        const o = opts || {};
        const all = Win.FsUtils.listDetailed(_norm(path), true).map(e => _complete(e));
        // hidden = Windows' hidden attribute, or a Linux-style ".name"
        let list = o.includeHidden === true ? all : all.filter(e => !e.hidden && !e.name.startsWith("."));
        const hidden = all.length - list.length;
        if (o.filters && o.filters.length > 0) {
            const res = o.filters.map(f => _glob(f));
            list = list.filter(e => e.isDir || res.some(r => r.test(e.name)));
        }
        const key = o.sort || "name";
        const desc = o.desc === true;
        const cmp = (a, b) => {
            let d = 0;
            if (key === "size")
                d = a.size - b.size;
            else if (key === "modified" || key === "mtime")
                d = a.mtimeMs - b.mtimeMs;
            else if (key === "type")
                d = String(a.mime).localeCompare(String(b.mime));
            if (d === 0)
                d = a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" });
            return desc ? -d : d;
        };
        list.sort((a, b) => (o.dirsFirst !== false && a.isDir !== b.isDir) ? (a.isDir ? -1 : 1) : cmp(a, b));
        return { entries: list, total: list.length, hidden: hidden };
    }
    function _later(callback, value) {
        if (callback)
            Qt.callLater(() => callback(value));
    }
    function _missing(path) {
        return { error: `${path}: no such file or folder`, code: "ENOENT" };
    }

    // ---- folders (watched by polling: the folder is re-read every 2 s while a browser shows it)
    property var _watches: ({})
    property int _nextId: 1

    function watch(path, options, callback) {
        const p = _norm(path);
        if (Win.FsUtils.classify(p) !== "dir") {
            _later(callback, _missing(p));
            return;
        }
        const id = `w${_nextId++}`;
        const r = _entries(p, _options(options));
        _watches[id] = { path: p, options: _options(options), sig: _sig(r.entries), seq: 0 };
        _later(callback, Object.assign({ watchId: id, watching: true, pollOnFocus: true, seq: 0, cursor: "" }, r));
    }
    function page(watchId, options, callback) {
        const w = _watches[watchId];
        if (!w) {
            _later(callback, { error: "not watched", code: "ESTALE" });
            return;
        }
        w.options = _options(options);
        _later(callback, Object.assign({ watchId: watchId, seq: w.seq, cursor: "" }, _entries(w.path, w.options)));
    }
    function list(path, options, callback) {
        const p = _norm(path);
        if (Win.FsUtils.classify(p) !== "dir") {
            _later(callback, _missing(p));
            return;
        }
        const r = _entries(p, _options(options));
        if (options?.limit)
            r.entries = r.entries.slice(0, options.limit);
        _later(callback, r);
    }
    function unwatch(watchId) {
        delete _watches[watchId];
    }
    function _sig(entries) {
        return entries.map(e => `${e.name}|${e.mtimeMs}|${e.size}`).join("\n");
    }
    Timer {
        running: Object.keys(root._watches).length > 0
        interval: 2000
        repeat: true
        onTriggered: {
            for (const id of Object.keys(root._watches)) {
                const w = root._watches[id];
                if (Win.FsUtils.classify(w.path) !== "dir") {
                    root.watchEvent({ watchId: id, kind: "gone", seq: ++w.seq });
                    delete root._watches[id];
                    continue;
                }
                const sig = root._sig(root._entries(w.path, w.options).entries);
                if (sig !== w.sig) {
                    w.sig = sig;
                    root.watchEvent({ watchId: id, kind: "resync", seq: ++w.seq });
                }
            }
        }
    }

    // ---- single paths
    function stat(path, callback) {
        const p = _norm(path);
        const s = Win.FsUtils.statPath(p);
        _later(callback, s && s.path ? { entry: _complete(s), path: s.path, isDir: s.isDir, size: s.size, mtimeMs: s.mtimeMs, mime: s.mime } : _missing(p));
    }
    function count(paths, includeHidden, callback) {
        const counts = {};
        for (const path of paths || []) {
            const p = _norm(path);
            counts[path] = Win.FsUtils.classify(p) === "dir" ? { count: Win.FsUtils.listDetailed(p, includeHidden === true).length } : { code: "ENOTDIR" };
        }
        _later(callback, { counts: counts });
    }
    function thumbnails(paths, size, watchId, callback) {
        // pictures are shown from the file itself
        const results = (paths || []).map(p => {
            const s = Win.FsUtils.statPath(_norm(p));
            return { path: p, thumbnail: s?.thumbnailable ? `file:///${_norm(p)}` : "" };
        });
        _later(callback, { results: results });
    }
    function imageSize(path, callback) {
        const s = Win.ImageTools.imageSize(_norm(path));
        _later(callback, s.width > 0 ? { width: s.width, height: s.height } : { error: "not an image", code: "EINVAL" });
    }
    function preview(path, maxBytes, callback) {
        const p = _norm(path);
        const s = Win.FsUtils.statPath(p);
        if (!s?.path) {
            _later(callback, _missing(p));
            return;
        }
        previewFile.path = "";
        previewFile.pending = { path: p, maxBytes: maxBytes || 65536, size: s.size, callback: callback };
        previewFile.path = p;
    }
    FileView {
        id: previewFile

        property var pending: null

        printErrors: false
        blockLoading: false
        onLoaded: {
            const q = pending;
            pending = null;
            if (!q?.callback)
                return;
            const all = text();
            const binary = all.slice(0, 4096).indexOf("\u0000") >= 0;
            q.callback({ text: binary ? "" : all.slice(0, q.maxBytes), binary: binary, truncated: all.length > q.maxBytes, size: q.size });
        }
        onLoadFailed: {
            const q = pending;
            pending = null;
            q?.callback?.({ error: "could not read", code: "EIO" });
        }
    }
    function scanSize(paths, callback) {
        _later(callback, { error: "not supported", code: "ENOTSUP" });
    }
    function cancelScan(id) {}
    function resolveLocation(text, cwd, callback) {
        let t = String(text || "").trim().replace(/^file:\/\/\/?/, "");
        let p = _norm(t);
        if (!/^[A-Za-z]:\//.test(p))
            p = `${_norm(cwd).replace(/\/$/, "")}/${p}`;
        const kind = Win.FsUtils.classify(p);
        if (kind === "dir")
            _later(callback, { location: { kind: "folder", path: Win.FsUtils.canonicalPath(p) || p, select: [] } });
        else if (kind === "file")
            _later(callback, { location: { kind: "folder", path: p.replace(/\/[^/]*$/, "") || "C:/", select: [p] } });
        else
            _later(callback, _missing(p));
    }
    function copyPathToClipboard(path) {
        Win.Clipboard.copyText(_norm(path).replace(/\//g, "\\"));
    }

    // ---- search: names under a folder (6 levels deep)
    property int _searchId: 1
    function search(rootPath, query, options, callback) {
        const id = `s${_searchId++}`;
        const q = String(query || "").replace(/[*?]/g, "");
        _later(callback, { searchId: id, limit: 500 });
        Qt.callLater(() => {
            root.searchEvent({ searchId: id, kind: "backend", backend: "windows", phase: "", depthLimit: 6 });
            const paths = q ? Win.FsUtils.findFiles([_norm(rootPath)], [`*${q}*`], 1, 6, false).concat(Win.FsUtils.findFiles([_norm(rootPath)], [`*${q}*`], 1, 6, true)) : [];
            const entries = paths.slice(0, 500).map(p => _complete(Win.FsUtils.statPath(p))).filter(e => e?.path && (options?.includeHidden || !e.hidden));
            root.searchEvent({ searchId: id, kind: "results", entries: entries, count: entries.length });
            root.searchEvent({ searchId: id, kind: "done", count: entries.length, truncated: paths.length > 500 });
        });
    }
    function cancelSearch(id) {}

    // ---- changes (asked for by the user in the browser)
    function mkdir(path, callback) {
        const p = _norm(path);
        _later(callback, Win.FsUtils.makePath(p) ? { path: p } : { error: "could not create the folder", code: "EIO" });
    }
    function rename(path, name, callback) {
        _later(callback, Win.FsUtils.renamePath(_norm(path), name) ? { path: _norm(path).replace(/[^/]*$/, name) } : { error: "could not rename", code: "EIO" });
    }
    function trash(paths, callback) {
        _later(callback, Win.FsUtils.recycle((paths || []).map(p => _norm(p))) ? { trashed: paths } : { error: "could not move to the Recycle Bin", code: "EIO" });
    }

    function _options(options) {
        const opts = options || {};
        return {
            includeHidden: opts.includeHidden === true,
            filters: opts.filters || [],
            sort: opts.sortKey || opts.sort || "name",
            desc: opts.sortDesc === true || opts.desc === true,
            dirsFirst: opts.dirsFirst !== false,
            limit: opts.limit || 0
        };
    }
}
