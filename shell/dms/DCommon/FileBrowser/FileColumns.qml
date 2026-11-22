pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import qs.dms.DCommon.Common

Singleton {
    readonly property var optional: [
        {
            "id": "size",
            "sortKey": "size",
            "width": FileBrowserMetrics.sizeColumnWidth,
            "align": Text.AlignRight
        },
        {
            "id": "modified",
            "sortKey": "mtime",
            "width": FileBrowserMetrics.modifiedColumnWidth,
            "align": Text.AlignRight
        },
        {
            "id": "type",
            "sortKey": "type",
            "width": FileBrowserMetrics.typeColumnWidth,
            "align": Text.AlignLeft
        },
        {
            "id": "owner",
            "sortKey": "",
            "width": FileBrowserMetrics.ownerColumnWidth,
            "align": Text.AlignLeft
        },
        {
            "id": "permissions",
            "sortKey": "",
            "width": FileBrowserMetrics.permissionsColumnWidth,
            "align": Text.AlignLeft
        },
        {
            "id": "created",
            "sortKey": "",
            "width": FileBrowserMetrics.modifiedColumnWidth,
            "align": Text.AlignRight
        },
        {
            "id": "location",
            "sortKey": "",
            "width": FileBrowserMetrics.locationColumnWidth,
            "align": Text.AlignLeft
        }
    ]

    readonly property var trashColumns: [
        {
            "id": "origin",
            "sortKey": "origin",
            "width": FileBrowserMetrics.locationColumnWidth,
            "align": Text.AlignLeft
        },
        {
            "id": "deleted",
            "sortKey": "deleted",
            "width": FileBrowserMetrics.modifiedColumnWidth,
            "align": Text.AlignRight
        }
    ]

    readonly property var recentColumns: [
        {
            "id": "used",
            "sortKey": "used",
            "width": FileBrowserMetrics.modifiedColumnWidth,
            "align": Text.AlignRight
        }
    ]

    function label(id) {
        switch (id) {
        case "name":
            return I18n.tr("Name", "file browser sort criterion option");
        case "size":
            return I18n.tr("Size", "file browser sort criterion option");
        case "modified":
            return I18n.tr("Modified", "file browser sort criterion option");
        case "type":
            return I18n.tr("Type", "file browser sort criterion option");
        case "owner":
            return I18n.tr("Owner", "file list column header");
        case "permissions":
            return I18n.tr("Permissions", "file list column header");
        case "created":
            return I18n.tr("Created", "file list column header");
        case "location":
            return I18n.tr("Location", "file list column header");
        case "origin":
            return I18n.tr("Original location", "trash list column header, where an item was deleted from");
        case "deleted":
            return I18n.tr("Deleted", "trash list column header, when an item was moved to the trash");
        case "used":
            return I18n.tr("Last used", "recent files list column header, when a file was last opened");
        default:
            return id;
        }
    }

    function value(id, entry, childCount) {
        switch (id) {
        case "size":
            {
                if (!entry.isDir)
                    return FileFormat.size(entry.size);
                const count = childCount ?? entry.childCount ?? -1;
                return count >= 0 ? FileFormat.count(count) : "";
            }
        case "modified":
            return FileFormat.modified(entry.mtimeMs);
        case "type":
            return FileFormat.typeLabel(entry);
        case "owner":
            return entry.owner;
        case "permissions":
            return entry.mode;
        case "created":
            return FileFormat.modified(entry.ctimeMs);
        case "location":
            return FileFormat.location(entry.path);
        case "origin":
            return FileFormat.location(entry.originalPath ?? "");
        case "deleted":
            return FileFormat.modified(entry.deletedMs);
        case "used":
            return FileFormat.modified(entry.usedMs);
        default:
            return "";
        }
    }

    function specFor(id) {
        return optional.find(column => column.id === id) ?? trashColumns.find(column => column.id === id) ?? recentColumns.find(column => column.id === id) ?? null;
    }

    function sortKeyFor(id) {
        return specFor(id)?.sortKey ?? "";
    }

    function widthFor(id, widths) {
        return widths[id] ?? specFor(id)?.width ?? FileBrowserMetrics.columnMinWidth;
    }
}
