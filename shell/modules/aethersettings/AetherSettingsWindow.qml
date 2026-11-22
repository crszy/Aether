pragma ComponentBehavior: Bound
// Aether settings: every shell's settings in one window, drawn the DarkShadowShell way (navigation rail, big
// page title, search, tabs, shared-axis page motion, shooting stars). The layout follows DarkShadowShell's
// quickshell/rice-settings/shell.qml (MIT, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell).
import QtQuick
import Quickshell
import Quickshell.Windows as Win
import qs.services as Cae
import QtQuick.Effects
import qs.modules.aether as Ae
import "catalog.js" as Cat

FloatingWindow {
    id: win

    property string startTab: ""
    property bool closing: false

    // every tab of every group, in rail order; a PageSlot each
    readonly property var flat: {
        const out = [];
        Cat.groups.forEach((g, gi) => g.tabs.forEach((t, ti) => out.push({ group: g.id, groupIdx: gi, groupName: g.name, tab: t.id, tabIdx: ti, name: t.name, page: t.page || "", piece: t.piece || "", words: (g.name + " " + t.name + " " + (t.piece && Cat.pieces[t.piece] ? Cat.pieces[t.piece].desc : "")).toLowerCase() })));
        return out;
    }
    property int curIdx: 0
    readonly property var cur: flat[curIdx] || flat[0]
    readonly property var curGroup: Cat.groups[cur.groupIdx]
    // PageSlot reads these: which way the pages slide
    property int navX: 0
    property int navY: 0
    property bool searchWanted: false
    function pageLoaded() {}

    property string query: ""
    readonly property var hits: query.length < 2 ? [] : flat.filter(t => t.words.indexOf(query.toLowerCase()) >= 0).slice(0, 8)

    function show(i) {
        if (i < 0 || i >= flat.length || i === curIdx)
            return;
        const a = flat[curIdx], b = flat[i];
        navY = a.groupIdx === b.groupIdx ? 0 : (b.groupIdx > a.groupIdx ? 1 : -1);
        navX = a.groupIdx === b.groupIdx ? (b.tabIdx > a.tabIdx ? 1 : -1) : 0;
        Sfx.play(navX < 0 || navY < 0 ? "page-prev" : "page-next");
        curIdx = i;
    }
    function go(groupId, tabId) {
        const i = flat.findIndex(t => t.group === groupId && (!tabId || t.tab === tabId));
        show(i);
    }
    function goTab(tabId) {
        show(flat.findIndex(t => t.tab === tabId));
    }
    property bool picked: false
    function pick(t) {
        picked = true;
        show(flat.indexOf(t));
    }
    function open(tab) {
        if (tab)
            show(flat.findIndex(t => t.tab === tab || t.group === tab));
        visible = true;
        Qt.callLater(applyFrame);
    }
    function close() {
        if (closing)
            return;
        closing = true;
        visible = false;
    }
    function applyFrame() {
        const w = frame.Window.window;
        if (!w)
            return;
        Win.WindowFrame.hideTitleBar(w);
        Win.WindowFrame.setTopmost(w, Cae.WinSettings.nexusTopmost);
        Win.WindowFrame.bringToFront(w);
    }

    title: "Aether settings — " + cur.name
    color: "transparent"
    surfaceFormat.opaque: false
    implicitWidth: Prefs.winW(screen ? screen.width : 1600, Prefs.size)
    implicitHeight: Prefs.winH(screen ? screen.height : 900, Prefs.size)
    minimumSize.width: 860
    minimumSize.height: 560

    Component.onCompleted: {
        Prefs.shown = true;
        Sfx.warm();
        if (startTab) {
            const i = flat.findIndex(t => t.tab === startTab || t.group === startTab);
            if (i >= 0)
                curIdx = i;
        }
        Qt.callLater(applyFrame);
    }
    Component.onDestruction: Prefs.shown = false
    onVisibleChanged: if (visible) Qt.callLater(applyFrame)

    Connections {
        target: Cae.WinSettings
        function onNexusTopmostChanged() { win.applyFrame(); }
    }

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: 22
        color: M3.surface
        border.width: 1
        border.color: M3.alpha(M3.outlineVariant, 0.6)
        clip: true
        focus: true
        Keys.onEscapePressed: win.query !== "" ? (search.text = "") : win.close()

        readonly property int railW: 96

        StarField {
            anchors.fill: parent
            count: 14
            running: Prefs.stars && win.visible
            visible: Prefs.stars
            opacity: 0.55
        }

        // drag the window by any empty part of the header
        Item {
            x: frame.railW
            width: parent.width - frame.railW
            height: 84
            DragHandler {
                target: null
                onActiveChanged: if (active) frame.Window.window.startSystemMove()
            }
        }

        // rail
        Rectangle {
            id: rail
            width: frame.railW
            height: parent.height
            color: M3.alpha(M3.surfaceLow, 0.72)

            Rectangle {   // the Aether mark where DarkShadowShell has its profile blob
                x: (parent.width - 48) / 2
                y: 18
                width: 48
                height: 48
                radius: 16
                color: M3.primaryContainer
                Loader {   // the live Aether mark (modules/aether/AetherLogo, the splash logo)
                    anchors.centerIn: parent
                    sourceComponent: Ae.AetherLogo {
                        size: 22
                        arcColour: M3.primary
                    }
                }
                DragHandler {
                    target: null
                    onActiveChanged: if (active) frame.Window.window.startSystemMove()
                }
            }

            Flickable {
                y: 82
                width: parent.width
                height: parent.height - y - 8
                contentHeight: railCol.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: railCol
                    width: parent.width
                    Repeater {
                        model: Cat.groups
                        Item {
                            id: ri
                            required property var modelData
                            required property int index
                            readonly property bool on: win.cur.groupIdx === index
                            width: railCol.width
                            height: 56
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 4
                                width: ri.on || riArea.containsMouse ? 56 : 24
                                height: 32
                                radius: 16
                                color: ri.on ? M3.secondaryContainer : (riArea.containsMouse ? M3.alpha(M3.ink, 0.08) : "transparent")
                                Behavior on width { NumberAnimation { duration: M3.d(260); easing.type: M3.springy } }
                                Behavior on color { ColorAnimation { duration: 140 } }
                            }
                            Image {   // a shell's own tab (serpantinum) shows that shell's logo
                                visible: source != ""
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 9
                                width: 22
                                height: 22
                                source: Cat.logoFile(ri.modelData.id) ? "logos/" + Cat.logoFile(ri.modelData.id) : ""
                                sourceSize.width: 44
                                sourceSize.height: 44
                                fillMode: Image.PreserveAspectFit
                                opacity: ri.on ? 1 : 0.75
                                layer.enabled: !!Cat.darkLogos[ri.modelData.id]
                                layer.effect: MultiEffect { brightness: 0.85 }   // black mark -> light, like the icons
                            }
                            Icon {
                                visible: !Cat.logoFile(ri.modelData.id)
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 8
                                name: ri.modelData.icon
                                size: 22
                                color: ri.on ? M3.inkSecondaryC : M3.inkDim
                            }
                            T {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 38
                                text: ri.modelData.name
                                font.pixelSize: 11
                                font.weight: ri.on ? Font.DemiBold : Font.Normal
                                color: ri.on ? M3.ink : M3.inkDim
                            }
                            MouseArea {
                                id: riArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: win.go(ri.modelData.id, "")
                            }
                        }
                    }
                }
            }
        }

        // header
        T {
            x: frame.railW + 28
            y: 20
            text: win.cur.groupName.toUpperCase()
            font.pixelSize: 11
            font.letterSpacing: 1.4
            font.weight: Font.Medium
            color: M3.primary
        }
        T {
            kind: "title"
            x: frame.railW + 28
            y: 38
            width: search.x - x - 16
            elide: Text.ElideRight
            text: win.cur.name
            font.pixelSize: 30
            font.weight: Font.DemiBold
        }

        Field {
            id: search
            x: closeBtn.x - width - 12
            y: 22
            width: Math.min(280, Math.max(170, frame.width * 0.27))
            height: 40
            radius: 20
            placeholder: "Search settings"
            onTextChanged: { win.query = text; win.picked = false; }
            onAccepted: if (win.hits.length) win.pick(win.hits[0])
        }

        Rectangle {
            id: closeBtn
            x: frame.width - width - 18
            y: 22
            width: 40
            height: 40
            radius: 20
            color: closeArea.containsMouse ? M3.alpha(M3.ink, 0.1) : "transparent"
            Icon {
                anchors.centerIn: parent
                name: "close"
                glyph: "✕"
                size: 20
                color: M3.ink
            }
            MouseArea {
                id: closeArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: win.close()
            }
        }

        // tabs of the group (hidden when it has one)
        Item {
            id: tabs
            x: frame.railW + 20
            y: 84
            width: frame.width - x - 20
            height: 44
            visible: win.curGroup.tabs.length > 1
            Row {
                id: tabRow
                height: parent.height
                Repeater {
                    model: win.curGroup.tabs
                    Item {
                        id: ti
                        required property var modelData
                        required property int index
                        readonly property bool on: win.cur.tabIdx === index
                        width: tl.implicitWidth + 32
                        height: tabRow.height
                        T {
                            id: tl
                            anchors.centerIn: parent
                            text: ti.modelData.name
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: ti.on ? M3.primary : M3.inkDim
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.go(win.curGroup.id, ti.modelData.id)
                        }
                        onOnChanged: if (on) { underline.x = x + 12; underline.width = width - 24; }
                        Component.onCompleted: if (on) { underline.x = x + 12; underline.width = width - 24; }
                    }
                }
            }
            Rectangle {
                id: underline
                y: parent.height - 3
                height: 3
                radius: 1.5
                color: M3.primary
                Behavior on x { NumberAnimation { duration: M3.d(320); easing.type: Easing.BezierSpline; easing.bezierCurve: M3.emph } }
                Behavior on width { NumberAnimation { duration: M3.d(320); easing.type: Easing.BezierSpline; easing.bezierCurve: M3.emph } }
            }
            Rectangle {
                y: parent.height - 1
                width: parent.width
                height: 1
                color: M3.alpha(M3.outlineVariant, 0.5)
            }
        }

        // pages
        Item {
            id: pagesHolder
            x: frame.railW
            y: tabs.visible ? 136 : 92
            width: frame.width - frame.railW
            height: frame.height - y
            clip: true

            Repeater {
                model: win.flat
                PageSlot {
                    id: slot
                    required property var modelData
                    required property int index
                    idx: index
                    cur: win.curIdx
                    nav: win
                    page: modelData.piece ? pieceC : modelData.page === "home" ? homeC : modelData.page === "looks" ? looksC : modelData.page === "mix" ? mixC : modelData.page === "panel" ? panelC : modelData.page === "more" ? moreC : modelData.page === "serp" ? serpC : modelData.page === "aetherbar" ? aBarC : modelData.page === "aetherdock" ? aDockC : modelData.page === "aetherdash" ? aDashC : modelData.page === "dss" ? dssBarC : modelData.page === "dsswidgets" ? dssWidgetsC : modelData.page === "dssdock" ? dssDockC : modelData.page === "dsswm" ? wmC : modelData.page === "dsskeys" ? keysC : modelData.page === "dssmon" ? monC : aboutC

                    Component { id: pieceC; PiecePage { win: win; pieceId: slot.modelData.piece; query: slot.on ? win.query : "" } }
                    Component { id: homeC; HomePage { win: win } }
                    Component { id: looksC; LooksPage {} }
                    Component { id: mixC; MixPage { win: win } }
                    Component { id: panelC; PanelPage {} }
                    Component { id: moreC; MorePage { win: win } }
                    Component { id: aboutC; AboutPage {} }
                    Component { id: serpC; SerpPage {} }
                    Component { id: aBarC; AetherPage { win: win; section: "bar" } }
                    Component { id: aDockC; AetherPage { win: win; section: "dock" } }
                    Component { id: aDashC; AetherPage { win: win; section: "dash" } }
                    Component { id: dssBarC; DssPage { section: "bar" } }
                    Component { id: dssWidgetsC; DssPage { section: "widgets" } }
                    Component { id: dssDockC; DssPage { section: "dock" } }
                    Component { id: wmC; WmPage { section: "windows" } }
                    Component { id: keysC; WmPage { section: "keybinds" } }
                    Component { id: monC; WmPage { section: "monitors" } }
                }
            }
        }

        // search results (pages); the open page also hides options that do not match
        Rectangle {
            visible: win.hits.length > 0 && !win.picked
            x: search.x
            y: search.y + search.height + 6
            z: 50
            width: search.width
            height: hitCol.height + 12
            radius: M3.rM
            color: M3.surfaceHighest
            border.width: 1
            border.color: M3.alpha(M3.outlineVariant, 0.6)
            Column {
                id: hitCol
                y: 6
                width: parent.width
                Repeater {
                    model: win.hits
                    Rectangle {
                        id: hit
                        required property var modelData
                        width: hitCol.width
                        height: 44
                        color: hitArea.containsMouse ? M3.alpha(M3.ink, 0.08) : "transparent"
                        Column {
                            x: 14
                            anchors.verticalCenter: parent.verticalCenter
                            T {
                                text: hit.modelData.name
                                font.pixelSize: 13
                            }
                            T {
                                text: hit.modelData.groupName
                                font.pixelSize: 11
                                color: M3.inkDim
                            }
                        }
                        MouseArea {
                            id: hitArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.pick(hit.modelData)
                        }
                    }
                }
            }
        }

        // resize from the bottom-right corner (no system frame)
        MouseArea {
            x: frame.width - 16
            y: frame.height - 16
            width: 16
            height: 16
            z: 100
            cursorShape: Qt.SizeFDiagCursor
            onPressed: frame.Window.window.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
        }
    }
}
