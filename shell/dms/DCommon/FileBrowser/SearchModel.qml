pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import "NaturalSort.js" as NaturalSort

QtObject {
    id: root

    property var backend: Host.files
    property string rootPath: ""
    property string query: ""
    property var options: ({})
    property string thumbnailSize: "normal"
    property bool thumbnailsEnabled: true
    property real thumbnailSizeLimit: -1
    property string sortKey: ""
    property bool sortDescending: false

    property bool active: false
    property bool loading: false
    property string error: ""
    property string errorCode: ""
    property bool truncated: false
    property bool partial: false
    property int unreadable: 0
    property int total: 0
    property int limit: 0
    property string searchId: ""
    property string servedBy: ""
    property string indexPhase: ""
    property int depthLimit: 0

    readonly property string path: rootPath
    readonly property bool available: backend?.connected === true && typeof backend?.search === "function"
    readonly property ListModel entries: ListModel {}
    readonly property int count: entries.count
    readonly property bool pollOnFocus: false

    property int _firstVisible: -1
    property int _lastVisible: -1
    property var _token: null
    property var _pending: []
    property var _rows: []
    property var _byPath: new Map()
    property int _seq: 0

    signal listed
    signal pathReset
    signal finished

    readonly property ThumbnailRequests _thumbnails: ThumbnailRequests {
        backend: root.backend
        size: root.thumbnailSize
        sizeLimit: root.thumbnailSizeLimit
    }

    readonly property Connections _backendLink: Connections {
        target: root.backend
        ignoreUnknownSignals: true

        function onSearchEvent(data) {
            root._onEvent(data);
        }

        function onConnectedChanged() {
            if (!root.backend.connected && root.loading)
                root._fail("UNAVAILABLE", "file service unavailable");
        }
    }

    onSortKeyChanged: _resort()
    onSortDescendingChanged: _resort()

    Component.onDestruction: cancel()

    function start() {
        cancel();
        _reset();
        active = true;
        pathReset();
        if (rootPath === "")
            return;
        if (!available) {
            _fail("UNAVAILABLE", "file service unavailable");
            return;
        }

        const token = {
            "stale": false
        };
        _token = token;
        _pending = [];
        loading = true;
        backend.search(rootPath, query, options, result => {
            if (token.stale) {
                if (result.searchId)
                    backend.cancelSearch(result.searchId);
                return;
            }
            if (result.error) {
                _fail(result.code || "", result.error);
                return;
            }
            searchId = result.searchId || "";
            limit = result.limit || 0;
            const buffered = _pending;
            _pending = [];
            for (const event of buffered)
                _onEvent(event);
        });
    }

    function cancel() {
        if (_token)
            _token.stale = true;
        _token = null;
        _pending = [];
        if (searchId !== "" && backend)
            backend.cancelSearch(searchId);
        searchId = "";
        loading = false;
    }

    function clear() {
        cancel();
        active = false;
        _reset();
        pathReset();
    }

    function refresh() {
        if (active)
            start();
    }

    function setSort(key) {
        if (key === sortKey) {
            sortDescending = !sortDescending;
            return;
        }
        sortKey = key;
        sortDescending = false;
    }

    function loadMore() {
    }

    function indexOfPath(target) {
        const row = _byPath.get(target);
        return row ? _position(row) : -1;
    }

    function _position(row) {
        let low = 0;
        let high = _rows.length - 1;
        while (low <= high) {
            const mid = (low + high) >> 1;
            const order = _compare(_rows[mid], row);
            if (order === 0)
                return mid;
            if (order < 0)
                low = mid + 1;
            else
                high = mid - 1;
        }
        return -1;
    }

    function requestThumbnails(first, last) {
        if (first < 0 || last < first || !thumbnailsEnabled || !available)
            return;
        _firstVisible = first;
        _lastVisible = last;
        const paths = [];
        for (let i = first; i <= Math.min(last, _rows.length - 1); i++) {
            if (_thumbnails.wanted(_rows[i]))
                paths.push(_rows[i].path);
        }
        _thumbnails.request(paths, "", items => {
            for (const item of items) {
                const index = indexOfPath(item.path);
                if (index < 0)
                    continue;
                _rows[index].thumbnail = item.thumbnail;
                entries.setProperty(index, "thumbnail", item.thumbnail);
            }
        });
    }

    function _reset() {
        _thumbnails.reset();
        entries.clear();
        _rows = [];
        _byPath = new Map();
        _seq = 0;
        total = 0;
        truncated = false;
        partial = false;
        unreadable = 0;
        servedBy = "";
        indexPhase = "";
        depthLimit = 0;
        error = "";
        errorCode = "";
    }

    function _fail(code, message) {
        cancel();
        error = message;
        errorCode = code;
    }

    function _compare(a, b) {
        let order = 0;
        switch (sortKey) {
        case "name":
            order = a.nameKey.localeCompare(b.nameKey);
            break;
        case "size":
            order = a.size - b.size;
            break;
        case "mtime":
            order = a.mtimeMs - b.mtimeMs;
            break;
        case "location":
            order = _parent(a.path).localeCompare(_parent(b.path));
            break;
        default:
            order = (b.score ?? 0) - (a.score ?? 0);
            break;
        }
        if (order === 0)
            return a.seq - b.seq;
        return sortDescending ? -order : order;
    }

    function _parent(path) {
        return path.substring(0, path.lastIndexOf("/"));
    }

    function _insert(entry) {
        entry.seq = _seq++;
        entry.nameKey = NaturalSort.key(entry.name);
        let low = 0;
        let high = _rows.length;
        while (low < high) {
            const mid = (low + high) >> 1;
            if (_compare(_rows[mid], entry) <= 0)
                low = mid + 1;
            else
                high = mid;
        }
        _rows.splice(low, 0, entry);
        _byPath.set(entry.path, entry);
        entries.insert(low, entry);
    }

    function _resort() {
        if (_rows.length === 0)
            return;
        const rows = _rows.slice();
        rows.sort(_compare);
        _rows = rows;
        entries.clear();
        entries.append(rows);
        listed();
    }

    function _onEvent(data) {
        if (searchId === "" && _token !== null) {
            _pending.push(data);
            return;
        }
        if (data.searchId !== searchId)
            return;
        switch (data.kind) {
        case "backend":
            servedBy = data.backend || "";
            indexPhase = data.phase || "";
            depthLimit = data.depthLimit || 0;
            break;
        case "results":
            for (const entry of data.entries || [])
                _insert(entry);
            total = data.count || _rows.length;
            listed();
            break;
        case "done":
            total = data.count || _rows.length;
            truncated = data.truncated === true;
            partial = data.partial === true;
            unreadable = data.unreadable || 0;
            searchId = "";
            _token = null;
            loading = false;
            listed();
            finished();
            break;
        }
    }
}
