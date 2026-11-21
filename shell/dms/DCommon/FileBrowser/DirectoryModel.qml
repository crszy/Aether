pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import "NameFilter.js" as NameFilter

QtObject {
    id: root

    property var backend: Host.files
    property string path: ""
    property bool includeHidden: false
    property var filters: []
    property string sortKey: "name"
    property bool sortDesc: false
    property bool dirsFirst: true
    property bool hiddenLast: true
    property int pageSize: 500
    property string thumbnailSize: "normal"
    property string nameFilter: ""
    property bool thumbnailsEnabled: true
    property real thumbnailSizeLimit: -1
    property bool networkThumbnails: true

    property bool loading: false
    property string error: ""
    property string errorCode: ""
    property int total: 0
    property int hiddenCount: 0
    property string watchId: ""
    property bool watching: false
    property bool pollOnFocus: false
    property string cursor: ""

    readonly property bool available: backend?.connected === true
    readonly property ListModel entries: ListModel {}
    readonly property int count: entries.count
    readonly property bool hasMore: cursor !== ""

    property var _all: []
    property int _seq: 0
    property int _firstVisible: -1
    property int _lastVisible: -1
    property string _loadedPath: ""
    property var _requested: null
    property bool _awaiting: false
    property var _buffered: []
    property var _watchToken: null
    property var _pageToken: null
    property string _matcherQuery: ""
    property var _matcher: null

    signal gone
    signal listed
    signal entriesAdded
    signal entriesRemoved
    signal pathReset

    readonly property ThumbnailRequests _thumbnails: ThumbnailRequests {
        backend: root.backend
        size: root.thumbnailSize
        sizeLimit: root.thumbnailSizeLimit
    }

    readonly property Connections _backendLink: Connections {
        target: root.backend
        ignoreUnknownSignals: true

        function onConnectedChanged() {
            root.reload();
        }

        function onWatchEvent(data) {
            root._onEvent(data);
        }
    }

    onPathChanged: reload()
    onBackendChanged: reload()
    onNameFilterChanged: _refilter()
    Component.onCompleted: {
        if (_loadedPath !== path)
            reload();
    }
    Component.onDestruction: _release()

    function reload() {
        _release();
        _requested = null;
        loading = false;
        if (path !== _loadedPath) {
            _loadedPath = path;
            pathReset();
        }
        if (path === "") {
            _all = [];
            entries.clear();
            total = 0;
            hiddenCount = 0;
            return;
        }
        if (!available) {
            _fail("UNAVAILABLE", "file service unavailable");
            return;
        }

        const token = _token();
        const service = backend;
        _watchToken = token;
        _requested = _options();
        loading = true;
        _await();
        service.watch(path, _options(), result => {
            if (token.stale) {
                if (result.watchId)
                    service.unwatch(result.watchId);
                return;
            }
            loading = false;
            _awaiting = false;
            if (!_accept(result)) {
                _buffered = [];
                return;
            }

            watchId = result.watchId || "";
            watching = result.watching === true;
            pollOnFocus = result.pollOnFocus === true;
            _replace(result);
            _drain();
            listed();
        });
    }

    function loadMore() {
        if (cursor === "" || watchId === "" || loading)
            return;
        _requestPage(cursor, result => {
            if (result.error) {
                _pageFailed(result);
                return;
            }
            cursor = result.cursor || "";
            total = result.total || 0;
            hiddenCount = result.hidden || 0;
            const more = result.entries || [];
            _all = _all.concat(more);
            entries.append(more.filter(entry => _passes(entry)));
            listed();
        });
    }

    function _passes(entry) {
        if (nameFilter === "")
            return true;
        if (_matcher === null || _matcherQuery !== nameFilter) {
            _matcherQuery = nameFilter;
            _matcher = NameFilter.compile(nameFilter);
        }
        return NameFilter.entryMatches(_matcher, entry);
    }

    function _refilter() {
        entries.clear();
        entries.append(_all.filter(entry => _passes(entry)));
        if (_firstVisible >= 0)
            _thumbnailRetry.restart();
        listed();
    }

    function _filteredIndexBefore(rawIndex) {
        let count = 0;
        for (let i = 0; i < rawIndex && i < _all.length; i++) {
            if (_passes(_all[i]))
                count++;
        }
        return count;
    }

    function setSort(key, desc) {
        sortKey = key;
        sortDesc = desc === true;
        repage();
    }

    function refresh() {
        if (!pollOnFocus)
            return;
        repage();
    }

    readonly property Timer _thumbnailRetry: Timer {
        interval: Anims.durShort
        onTriggered: root._requestVisibleThumbnails()
    }

    function requestThumbnails(first, last) {
        if (first < 0 || last < first)
            return;
        _firstVisible = first;
        _lastVisible = last;
        _requestVisibleThumbnails();
    }

    function _requestVisibleThumbnails() {
        const first = _firstVisible;
        const last = _lastVisible;
        if (watchId === "" || first < 0 || last < first)
            return;
        if (!thumbnailsEnabled || (pollOnFocus && !networkThumbnails))
            return;

        const paths = [];
        for (let i = first; i <= Math.min(last, entries.count - 1); i++) {
            const entry = entries.get(i);
            if (_thumbnails.wanted(entry))
                paths.push(entry.path);
        }
        _thumbnails.request(paths, watchId, items => {
            const at = _positions();
            for (const item of items)
                _setThumbnail(item.path, item.thumbnail, at);
        });
    }

    function indexOfPath(target) {
        for (let i = 0; i < entries.count; i++) {
            if (entries.get(i).path === target)
                return i;
        }
        return -1;
    }

    function _options(cursorValue) {
        return {
            "includeHidden": includeHidden,
            "filters": filters ?? [],
            "sortKey": sortKey,
            "sortDesc": sortDesc,
            "dirsFirst": dirsFirst,
            "hiddenLast": hiddenLast,
            "limit": pageSize,
            "cursor": cursorValue || ""
        };
    }

    function _fail(code, message) {
        error = message;
        errorCode = code;
        _all = [];
        entries.clear();
        total = 0;
        hiddenCount = 0;
    }

    function _accept(result) {
        if (!result.error) {
            error = "";
            errorCode = "";
            return true;
        }
        _fail(result.code || "", result.error);
        return false;
    }

    function _sameOptions(a, b) {
        if (!a || !b)
            return false;
        if (a.includeHidden !== b.includeHidden || a.sortKey !== b.sortKey || a.sortDesc !== b.sortDesc || a.dirsFirst !== b.dirsFirst || a.hiddenLast !== b.hiddenLast || a.limit !== b.limit)
            return false;
        return a.filters.length === b.filters.length && a.filters.every((pattern, i) => pattern === b.filters[i]);
    }

    function repageIfChanged() {
        if (_sameOptions(_options(), _requested))
            return;
        repage();
    }

    function repage() {
        _requested = _options();
        if (watchId === "") {
            reload();
            return;
        }
        _await();
        _requestPage("", result => {
            if (result.error) {
                _pageFailed(result);
                return;
            }
            _awaiting = false;
            _accept(result);
            _replace(result);
            _drain();
            listed();
        });
    }

    function _token() {
        return {
            "stale": false
        };
    }

    function _requestPage(cursorValue, apply) {
        if (_pageToken)
            _pageToken.stale = true;
        const token = _token();
        _pageToken = token;
        loading = true;
        backend.page(watchId, _options(cursorValue), result => {
            if (token.stale)
                return;
            _pageToken = null;
            loading = false;
            apply(result);
        });
    }

    function _pageFailed(result) {
        const buffered = _buffered;
        const goneNow = buffered.some(event => event.watchId === watchId && event.kind === "gone");
        _release();
        if (goneNow) {
            gone();
            return;
        }
        _accept(result);
    }

    function _replace(result) {
        _seq = result.seq || 0;
        _all = (result.entries || []).slice();
        entries.clear();
        entries.append(_all.filter(entry => _passes(entry)));
        total = result.total || 0;
        hiddenCount = result.hidden || 0;
        cursor = result.cursor || "";
        if (_firstVisible >= 0)
            _thumbnailRetry.restart();
    }

    function _await() {
        _awaiting = true;
        _buffered = [];
    }

    function _drain() {
        const buffered = _buffered;
        _buffered = [];
        for (const event of buffered) {
            if (event.watchId !== watchId || event.seq <= _seq)
                continue;
            _onEvent(event);
        }
    }

    function _onEvent(data) {
        if (_awaiting) {
            _buffered.push(data);
            return;
        }
        if (watchId === "" || data.watchId !== watchId)
            return;
        if (data.kind === "gone") {
            _release();
            gone();
            return;
        }
        if (data.seq <= _seq)
            return;
        if (data.seq !== _seq + 1) {
            repage();
            _seq = data.seq;
            return;
        }
        _seq = data.seq;

        switch (data.kind) {
        case "batch":
            _applyBatch(data);
            break;
        case "resync":
            repage();
            break;
        case "thumbnails":
            _applyChanged(data.thumbnails || []);
            break;
        }
    }

    function _applyBatch(data) {
        if (data.hidden !== undefined)
            hiddenCount = data.hidden;
        let removed = false;
        for (const name of data.removed || []) {
            total = Math.max(0, total - 1);
            const raw = _rawIndexOfName(name);
            if (raw < 0)
                continue;
            const shown = _passes(_all[raw]);
            const index = _filteredIndexBefore(raw);
            _all.splice(raw, 1);
            if (!shown)
                continue;
            entries.remove(index);
            removed = true;
        }
        if (removed)
            entriesRemoved();

        _applyChanged(data.changed || []);

        const added = data.added || [];
        const at = data.addedAt || [];
        let inserted = false;
        for (let i = 0; i < added.length; i++) {
            total++;
            const index = at[i] ?? _all.length;
            if (index > _all.length || (index === _all.length && hasMore))
                continue;
            const filteredIndex = _filteredIndexBefore(index);
            _all.splice(index, 0, added[i]);
            if (!_passes(added[i]))
                continue;
            entries.insert(filteredIndex, added[i]);
            inserted = true;
        }
        if (inserted)
            entriesAdded();
    }

    function _applyChanged(changed) {
        if (changed.length === 0)
            return;
        const at = _positions();
        let awaitsThumbnail = false;
        for (const entry of changed) {
            const raw = at.raw.get(entry.path);
            if (raw === undefined)
                continue;
            _all[raw] = entry;
            const index = at.shown.get(entry.path);
            if (index !== undefined)
                entries.set(index, entry);
            awaitsThumbnail = awaitsThumbnail || (entry.thumbnailable && entry.thumbnail === "");
        }
        if (awaitsThumbnail)
            _thumbnailRetry.restart();
    }

    function _rawIndexOfName(name) {
        return _all.findIndex(entry => entry.name === name);
    }

    function _positions() {
        const raw = new Map();
        const shown = new Map();
        let visible = 0;
        for (let i = 0; i < _all.length; i++) {
            const entry = _all[i];
            raw.set(entry.path, i);
            if (_passes(entry))
                shown.set(entry.path, visible++);
        }
        return {
            "raw": raw,
            "shown": shown
        };
    }

    function _setThumbnail(target, thumbnail, at) {
        const raw = at.raw.get(target);
        if (raw === undefined)
            return;
        _all[raw].thumbnail = thumbnail;
        const index = at.shown.get(target);
        if (index === undefined)
            return;
        entries.setProperty(index, "thumbnail", thumbnail);
    }

    function _release() {
        if (_watchToken)
            _watchToken.stale = true;
        if (_pageToken)
            _pageToken.stale = true;
        _watchToken = null;
        _pageToken = null;
        _awaiting = false;
        _buffered = [];
        if (watchId !== "" && backend)
            backend.unwatch(watchId);
        _thumbnails.reset();
        watchId = "";
        watching = false;
        pollOnFocus = false;
        cursor = "";
        _seq = 0;
    }
}
