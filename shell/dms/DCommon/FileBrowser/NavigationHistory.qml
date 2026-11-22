pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    id: root

    readonly property int depthLimit: 50

    property string path: ""
    property var backStack: []
    property var forwardStack: []
    property var marker: null

    readonly property bool canGoBack: backStack.length > 0
    readonly property bool canGoForward: forwardStack.length > 0
    readonly property string parentPath: FilePaths.parentOf(path)
    readonly property bool canGoUp: parentPath !== ""

    signal restored(var marker)
    signal entering(string target)

    function _entry() {
        return {
            "path": path,
            "marker": marker
        };
    }

    function _enter(entry) {
        entering(entry.path);
        path = entry.path;
        marker = entry.marker ?? null;
        restored(marker);
    }

    function _capped(stack) {
        return stack.length > depthLimit ? stack.slice(stack.length - depthLimit) : stack;
    }

    function go(target) {
        if (target === "" || target === path)
            return;
        const previous = _entry();
        forwardStack = [];
        if (previous.path !== "")
            backStack = _capped(backStack.concat([previous]));
        _enter({
            "path": target
        });
    }

    function back() {
        if (!canGoBack)
            return;
        const stack = backStack.slice();
        const target = stack.pop();
        forwardStack = forwardStack.concat([_entry()]);
        backStack = stack;
        _enter(target);
    }

    function forward() {
        if (!canGoForward)
            return;
        const stack = forwardStack.slice();
        const target = stack.pop();
        backStack = _capped(backStack.concat([_entry()]));
        forwardStack = stack;
        _enter(target);
    }

    function up() {
        go(parentPath);
    }

    function serialize() {
        return {
            "path": path,
            "back": backStack.map(entry => entry.path),
            "forward": forwardStack.map(entry => entry.path)
        };
    }

    function restore(state) {
        const paths = entries => (entries ?? []).map(value => ({
                        "path": value,
                        "marker": null
                    }));
        backStack = _capped(paths(state?.back));
        forwardStack = paths(state?.forward);
        entering(state?.path ?? "");
        path = state?.path ?? "";
        marker = null;
    }
}
