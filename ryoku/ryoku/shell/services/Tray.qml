pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Services.SystemTray

// Windows (Aether): Ryoku's tray from the Windows notification area (Quickshell's SystemTray on Windows) instead of
// the daemon's StatusNotifier host (D-Bus). Same view as upstream services/Tray.qml: `items` frames shaped like
// tray.go's ({service, id, title, status, iconPath, tooltip, itemIsMenu, menu}) and the same click intents. An item's
// menu is read through QsMenuOpener (as Caelestia's tray does) and published as the {children: [...]} tree Ryoku draws;
// menuEvent triggers the entry. iconPath carries the icon URL as is (ryoku-port.js keeps non-file URLs intact).
Singleton {
    id: root

    readonly property var sources: SystemTray.items.values.filter(i => i.status !== Status.Passive)
    property int menuRev: 0
    property var openers: ({})
    readonly property var items: {
        root.menuRev;
        return root.sources.map(it => root.frame(it));
    }

    function key(it) {
        return `${it.id}|${it.title}`;
    }

    function statusName(s) {
        return s === Status.NeedsAttention ? "NeedsAttention" : s === Status.Passive ? "Passive" : "Active";
    }

    function menuNode(entries) {
        return {
            id: 0,
            children: (entries ?? []).map((e, i) => ({
                id: i,
                label: e.text ?? "",
                type: e.isSeparator ? "separator" : "standard",
                enabled: e.enabled !== false,
                visible: true,
                children: []
            }))
        };
    }

    function frame(it) {
        const opener = root.openers[root.key(it)];
        const entries = it.hasMenu && opener ? opener.children.values : [];
        return {
            service: root.key(it),
            id: String(it.id ?? ""),
            title: it.title || it.tooltipTitle || "",
            status: root.statusName(it.status),
            category: "ApplicationStatus",
            iconPath: String(it.icon ?? ""),
            iconName: "",
            tooltip: { title: it.tooltipTitle ?? "", description: it.tooltipDescription ?? "", iconName: "" },
            itemIsMenu: it.onlyMenu === true,
            menu: entries.length > 0 ? root.menuNode(entries) : undefined
        };
    }

    function find(service) {
        return root.sources.find(it => root.key(it) === service) ?? null;
    }

    function activate(service, x, y) {
        const it = find(service);
        if (it)
            it.activate();
    }

    // no menu tree: Windows' own right click on the icon (the app shows its menu)
    function contextMenu(service, x, y) {
        const it = find(service);
        if (it)
            it.secondaryActivate();
    }

    function scroll(service, delta, orientation) {
        const it = find(service);
        if (it)
            it.scroll(delta, orientation === "horizontal");
    }

    function aboutToShow(service) {
        root.menuRev++;
    }

    function menuEvent(service, id) {
        const opener = root.openers[service];
        const entry = opener ? opener.children.values[Number(id)] : null;
        if (entry && !entry.isSeparator)
            entry.triggered();
    }

    function send(method, args) {
        const a = args ?? {};
        switch (method) {
        case "tray.activate":
            return activate(a.service, a.x, a.y);
        case "tray.secondaryActivate":
        case "tray.contextMenu":
            return contextMenu(a.service, a.x, a.y);
        case "tray.scroll":
            return scroll(a.service, a.delta, a.orientation);
        case "tray.aboutToShow":
            return aboutToShow(a.service);
        case "tray.menuEvent":
            return menuEvent(a.service, a.item);
        }
    }

    Instantiator {
        model: root.sources

        delegate: QsMenuOpener {
            required property var modelData

            menu: modelData.hasMenu ? modelData.menu : null
            onChildrenChanged: root.menuRev++
        }

        onObjectAdded: (index, object) => {
            const next = Object.assign({}, root.openers);
            next[root.key(object.modelData)] = object;
            root.openers = next;
            root.menuRev++;
        }
        onObjectRemoved: (index, object) => {
            const next = Object.assign({}, root.openers);
            delete next[root.key(object.modelData)];
            root.openers = next;
        }
    }
}
