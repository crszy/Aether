pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    id: root

    property var paths: []
    property string anchorPath: ""
    property string cursorPath: ""
    property bool keyboardCursor: false

    property var _base: []

    readonly property int count: paths.length
    readonly property bool empty: paths.length === 0

    readonly property var _set: {
        const set = ({});
        for (const path of paths)
            set[path] = true;
        return set;
    }

    function contains(path) {
        return _set[path] === true;
    }

    function showsCursor(path) {
        return keyboardCursor && cursorPath === path && !(paths.length === 1 && _set[path] === true);
    }

    function clear() {
        paths = [];
        anchorPath = "";
        cursorPath = "";
        keyboardCursor = false;
        _base = [];
    }

    function select(path) {
        paths = [path];
        anchorPath = path;
        cursorPath = path;
        _base = [];
    }

    function replace(next, cursor) {
        paths = next;
        anchorPath = cursor;
        cursorPath = cursor;
        _base = next.slice();
    }

    function toggle(path) {
        paths = contains(path) ? paths.filter(entry => entry !== path) : paths.concat([path]);
        anchorPath = path;
        cursorPath = path;
        _base = paths.slice();
    }

    function add(path) {
        if (contains(path))
            return;
        paths = paths.concat([path]);
    }

    function selectAll(model) {
        const all = [];
        for (let i = 0; i < model.count; i++)
            all.push(model.get(i).path);
        paths = all;
        _base = [];
        if (all.length > 0 && cursorPath === "")
            cursorPath = all[0];
    }

    function invert(model) {
        const kept = [];
        for (let i = 0; i < model.count; i++) {
            const path = model.get(i).path;
            if (!contains(path))
                kept.push(path);
        }
        paths = kept;
        _base = [];
        anchorPath = "";
        cursorPath = kept.length > 0 ? kept[0] : "";
    }

    function extendTo(model, index) {
        if (index < 0 || index >= model.count)
            return;
        const target = model.get(index).path;
        const anchor = anchorPath === "" ? target : anchorPath;
        const from = indexOf(model, anchor);
        if (from < 0) {
            select(target);
            return;
        }

        const range = [];
        const inRange = ({});
        for (let i = Math.min(from, index); i <= Math.max(from, index); i++) {
            const path = model.get(i).path;
            range.push(path);
            inRange[path] = true;
        }

        paths = _base.filter(path => inRange[path] !== true).concat(range);
        anchorPath = anchor;
        cursorPath = target;
    }

    function indexOf(model, path) {
        for (let i = 0; i < model.count; i++) {
            if (model.get(i).path === path)
                return i;
        }
        return -1;
    }

    function prune(model) {
        if (paths.length === 0)
            return;
        const live = ({});
        for (let i = 0; i < model.count; i++)
            live[model.get(i).path] = true;

        const kept = paths.filter(path => live[path] === true);
        if (kept.length !== paths.length)
            paths = kept;
        _base = _base.filter(path => live[path] === true);
        if (cursorPath !== "" && live[cursorPath] !== true)
            cursorPath = "";
        if (anchorPath !== "" && live[anchorPath] !== true)
            anchorPath = "";
    }
}
