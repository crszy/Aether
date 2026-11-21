pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import qs.dms.DCommon.Common

Singleton {
    readonly property var legacyKeys: ["sortBy", "sortAscending", "iconSizeIndex"]
    readonly property bool showExtensions: Style.settings?.showExtensions ?? true
    readonly property string dateFormat: Style.settings?.dateFormat ?? "locale"

    function sortKeyFor(value) {
        switch (value) {
        case "size":
        case "type":
        case "mtime":
            return value;
        case "modified":
            return "mtime";
        default:
            return "name";
        }
    }

    function load(bucket) {
        const saved = Host.cache?.fileBrowserSettings?.[bucket || "default"] ?? ({});
        return {
            "viewMode": saved.viewMode ?? "",
            "sortKey": sortKeyFor(saved.sortKey ?? saved.sortBy),
            "sortDesc": saved.sortDesc ?? saved.sortAscending === false,
            "gridZoom": saved.gridZoom ?? saved.iconSizeIndex ?? FileBrowserMetrics.defaultGridZoom,
            "listZoom": saved.listZoom ?? FileBrowserMetrics.defaultListZoom,
            "showSidebar": saved.showSidebar ?? true,
            "showPreview": saved.showPreview ?? true,
            "showHidden": saved.showHidden,
            "lastPath": FilePaths.normalize(saved.lastPath ?? "")
        };
    }

    function migrated(saved) {
        const record = Object.assign({}, saved);
        if (typeof record.lastPath === "string")
            record.lastPath = FilePaths.normalize(record.lastPath);
        if (record.sortKey === undefined && record.sortBy !== undefined)
            record.sortKey = sortKeyFor(record.sortBy);
        if (record.sortDesc === undefined && record.sortAscending !== undefined)
            record.sortDesc = record.sortAscending === false;
        if (record.gridZoom === undefined && record.iconSizeIndex !== undefined)
            record.gridZoom = record.iconSizeIndex;
        for (const legacy of legacyKeys)
            delete record[legacy];
        return record;
    }

    function save(bucket, patch) {
        const cache = Host.cache;
        if (!cache)
            return;
        const key = bucket || "default";
        const all = Object.assign({}, cache.fileBrowserSettings ?? ({}));
        const record = Object.assign(migrated(all[key] ?? ({})), patch);
        all[key] = record;
        cache.fileBrowserSettings = all;
        cache.saveCache();
    }
}
