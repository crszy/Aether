// Roadmap browser, rendered the way roadmap.sh renders it.
//
// The layout is not invented here. https://roadmap.sh/<slug>.json is the same
// document the website draws, and it carries every node's canvas position and
// size plus the edges between them, so scripts/roadmap_graph.sh normalises that
// to a zero-based coordinate space and this file just places boxes and draws
// the connectors. That is why the shape matches the real roadmap rather than
// being a tree guessed from the labels.
//
// Progress is per node id in ~/.config/zenith/roadmap_progress.json, so ticking
// a box survives restarts and does not touch the cached roadmap data.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Shapes
import Quickshell
import Quickshell.Io
import "../../../"
import "../../../services"
import "../../../Settings"

ColumnLayout {
    id: root
    spacing: Theme.scaled(8)

    property string searchText: ""
    property string activeSlug: ""
    property string activeTitle: ""
    property bool listLoading: true
    property bool listFailed: false
    property bool graphLoading: false

    property int graphW: 0
    property int graphH: 0
    property var edgeList: []

    // { slug: { nodeId: true } } -- only completed ids are stored.
    property var doneMap: ({})
    property int doneCount: 0

    // Detail panel, the equivalent of roadmap.sh's side sheet.
    property string detailId: ""
    property string detailTitle: ""
    property string detailBody: ""
    property var detailResources: []
    property bool detailLoading: false
    property bool detailDone: false

    // { slug: lastOpenedUnixSeconds } -- drives the ordering of the picker so
    // whatever you are actually working through sits at the front.
    property var recent: ({})

    // { slug: togglableNodeCount } -- learned when a roadmap is opened, so a
    // chip can say 102/139 rather than just 102. A roadmap never opened has no
    // entry and shows no bar, which is honest: we do not know its size yet.
    property var totals: ({})

    function doneFor(slug) {
        var m = root.doneMap ? root.doneMap[slug] : null;
        if (!m) return 0;
        var n = 0;
        for (var k in m) if (m[k]) n++;
        return n;
    }
    function totalFor(slug) {
        return (root.totals && root.totals[slug]) ? root.totals[slug] : 0;
    }
    property int togglableCount: 0

    readonly property string progressPath: PathSettings.configDir + "/zenith/roadmap_progress.json"

    readonly property var wordFixes: ({
        "ai": "AI", "api": "API", "aws": "AWS", "bi": "BI", "css": "CSS",
        "html": "HTML", "sql": "SQL", "ios": "iOS", "php": "PHP", "qa": "QA",
        "ux": "UX", "ui": "UI", "cpp": "C++", "c": "C", "dba": "DBA",
        "devops": "DevOps", "devsecops": "DevSecOps", "devrel": "DevRel",
        "mlops": "MLOps", "graphql": "GraphQL", "nextjs": "Next.js",
        "nodejs": "Node.js", "aspnet": "ASP.NET", "github": "GitHub",
        "javascript": "JavaScript", "typescript": "TypeScript",
        "leetcode": "LeetCode", "mongodb": "MongoDB", "postgresql": "PostgreSQL",
        "wordpress": "WordPress", "openclaw": "OpenClaw"
    })

    function prettify(slug) {
        var parts = String(slug).split("-"), out = [];
        for (var i = 0; i < parts.length; i++) {
            var w = parts[i];
            if (w === "") continue;
            out.push(wordFixes[w] !== undefined ? wordFixes[w]
                                                : w.charAt(0).toUpperCase() + w.slice(1));
        }
        return out.join(" ");
    }

    function isTogglable(t) { return t === "topic" || t === "subtopic"; }

    function openDetail(index) {
        var row = nodeModel.get(index);
        if (!row || !isTogglable(row.type)) return;
        root.detailId = row.id;
        root.detailTitle = row.label;
        root.detailBody = "";
        root.detailResources = [];
        root.detailDone = row.done;
        root.detailLoading = true;
        contentProc.command = ["bash", PathSettings.scriptsDir + "/roadmap_content.sh",
                               root.activeSlug, row.id];
        contentProc.running = false;
        contentProc.running = true;
    }

    function closeDetail() { root.detailId = ""; }

    function toggleDetailDone() {
        for (var i = 0; i < nodeModel.count; i++) {
            if (nodeModel.get(i).id === root.detailId) {
                toggleNode(i);
                root.detailDone = nodeModel.get(i).done;
                return;
            }
        }
    }

    // roadmap.sh encodes "can I skip this?" in data.legend. No legend means
    // core material; the three legend colours mean recommendation, alternative
    // and free-order respectively. Labels differ slightly between roadmaps
    // ("Order not strict" vs "Order in Roadmap not Strict"), so match loosely.
    function legendKind(lg) {
        var l = String(lg || "").toLowerCase();
        if (l === "") return "core";
        if (l.indexOf("recommend") !== -1) return "recommended";
        if (l.indexOf("alternative") !== -1) return "alternative";
        if (l.indexOf("strict") !== -1 || l.indexOf("anytime") !== -1) return "anyorder";
        return "optional";
    }
    function legendText(kind) {
        return kind === "core"        ? "Core - learn it"
             : kind === "recommended" ? "Recommended"
             : kind === "alternative" ? "Alternative - pick one"
             : kind === "anyorder"    ? "Any order"
                                      : "Optional";
    }

    function isDone(id) {
        var slugMap = doneMap[activeSlug];
        return !!(slugMap && slugMap[id]);
    }

    function recountDone() {
        var n = 0;
        for (var i = 0; i < nodeModel.count; i++)
            if (nodeModel.get(i).done) n++;
        doneCount = n;
    }

    function toggleNode(index) {
        var row = nodeModel.get(index);
        if (!row || !isTogglable(row.type)) return;

        var slugMap = doneMap[activeSlug] || {};
        if (slugMap[row.id]) delete slugMap[row.id];
        else slugMap[row.id] = true;

        doneMap[activeSlug] = slugMap;
        doneMap = doneMap;                       // re-notify: property is a plain object
        nodeModel.setProperty(index, "done", !row.done);
        recountDone();
        saveTimer.restart();
    }

    ListModel { id: allModel }
    ListModel { id: filteredModel }
    ListModel { id: nodeModel }
    ListModel { id: legendModel }

    function applyFilter() {
        var needle = root.searchText.trim().toLowerCase();
        filteredModel.clear();
        for (var i = 0; i < allModel.count; i++) {
            var row = allModel.get(i);
            if (needle === ""
                || row.title.toLowerCase().indexOf(needle) !== -1
                || row.slug.toLowerCase().indexOf(needle) !== -1)
                filteredModel.append({ slug: row.slug, title: row.title });
        }
        var stillShown = false;
        for (var j = 0; j < filteredModel.count; j++)
            if (filteredModel.get(j).slug === root.activeSlug) { stillShown = true; break; }
        if (!stillShown && filteredModel.count > 0)
            selectSlug(filteredModel.get(0).slug, filteredModel.get(0).title);
    }

    function selectSlug(slug, title) {
        if (!slug) return;
        closeDetail();
        // Remembered so the picker can lead with what is actually in progress.
        var seen = root.recent || ({});
        seen[slug] = Math.floor(Date.now() / 1000);
        root.recent = seen;
        saveTimer.restart();
        root.activeSlug = slug;
        root.activeTitle = title || prettify(slug);
        nodeModel.clear();
        root.edgeList = [];
        root.graphW = 0; root.graphH = 0;
        root.doneCount = 0; root.togglableCount = 0;
        root.graphLoading = true;
        graphProc.command = ["bash", PathSettings.scriptsDir + "/roadmap_graph.sh", slug];
        graphProc.running = false;
        graphProc.running = true;
    }

    // ---------------- data ----------------
    // Nothing here runs until the tab is on screen. Discovery hits the
    // network and the graph fetch spawns curl+jq, and doing that at shell
    // start -- for a tab nobody has opened -- is work for nothing.
    property bool started: false

    function startIfNeeded() {
        if (started || !visible) return;
        started = true;
        progressLoad.running = true;
        listProc.running = true;
    }

    onVisibleChanged: startIfNeeded()
    Component.onCompleted: startIfNeeded()

    Process {
        id: listProc
        command: ["bash", PathSettings.scriptsDir + "/roadmap_list.sh"]
        running: false
        stdout: StdioCollector {
            onStreamFinished: {
                root.listLoading = false;
                var slugs = [];
                try { slugs = JSON.parse(String(text).trim() || "[]"); } catch (e) { slugs = []; }
                allModel.clear();
                var rows = [];
                for (var i = 0; i < slugs.length; i++)
                    rows.push({ slug: slugs[i], title: root.prettify(slugs[i]) });
                // Most recently opened first, then anything with progress, then the
        // rest alphabetically -- the roadmap you are working through should not
        // be somewhere down an alphabetical list of ninety-one.
        rows.sort(function (a, b) {
            var ra = (root.recent && root.recent[a.slug]) || 0;
            var rb = (root.recent && root.recent[b.slug]) || 0;
            if (ra !== rb) return rb - ra;
            var pa = (root.doneMap && root.doneMap[a.slug]) ? 1 : 0;
            var pb = (root.doneMap && root.doneMap[b.slug]) ? 1 : 0;
            if (pa !== pb) return pb - pa;
            return a.title.localeCompare(b.title);
        });
                for (var j = 0; j < rows.length; j++) allModel.append(rows[j]);
                root.listFailed = allModel.count === 0;
                root.applyFilter();
            }
        }
    }

    Process {
        id: graphProc
        // One document, one parse: the graph is a few hundred KB at most and
        // arrives as a single JSON object rather than a stream.
        stdout: StdioCollector {
            onStreamFinished: {
                root.graphLoading = false;
                var g = null;
                try { g = JSON.parse(String(text).trim() || "null"); } catch (e) { g = null; }
                if (!g || !g.nodes) { root.graphW = 0; root.graphH = 0; return; }

                root.graphW = g.w || 0;
                root.graphH = g.h || 0;
                root.edgeList = g.edges || [];

                var togglable = 0;
                nodeModel.clear();
                for (var i = 0; i < g.nodes.length; i++) {
                    var n = g.nodes[i];
                    if (root.isTogglable(n.type)) togglable++;
                    nodeModel.append({
                        id: String(n.id), type: String(n.type),
                        nx: n.x | 0, ny: n.y | 0, nw: n.w | 0, nh: n.h | 0,
                        label: String(n.label || ""), fs: n.fs | 0,
                        lg: String(n.lg || ""), lc: String(n.lc || ""),
                        done: root.isDone(String(n.id))
                    });
                }

                // Only the categories this roadmap actually uses -- several
                // have no optional nodes at all, and a legend listing things
                // that are not on screen is worse than none.
                legendModel.clear();
                var seen = {};
                var order = ["core", "recommended", "alternative", "anyorder", "optional"];
                for (var k = 0; k < g.nodes.length; k++) {
                    var gn = g.nodes[k];
                    if (!root.isTogglable(gn.type)) continue;
                    var kind = root.legendKind(gn.lg);
                    if (!seen[kind]) seen[kind] = String(gn.lc || "");
                }
                for (var o = 0; o < order.length; o++) {
                    if (seen[order[o]] === undefined) continue;
                    legendModel.append({
                        kind: order[o],
                        ltext: root.legendText(order[o]),
                        lswatch: seen[order[o]] !== "" ? seen[order[o]] : String(Theme.accentColor)
                    });
                }
                root.togglableCount = togglable;
                var sizes = root.totals || ({});
                sizes[root.activeSlug] = togglable;
                root.totals = sizes;
                saveTimer.restart();
                root.recountDone();
                graphView.userZoomed = false;
                graphView.fitToWidth();
                graphView.contentX = 0;
                graphView.contentY = 0;
            }
        }
    }

    Process {
        id: contentProc
        stdout: StdioCollector {
            onStreamFinished: {
                root.detailLoading = false;
                var res = {};
                try { res = JSON.parse(String(text).trim() || "{}"); }
                catch (e) { root.detailBody = "Could not load this topic."; return; }
                if (res.title) root.detailTitle = res.title;
                root.detailBody = res.description || (res.missing
                    ? "roadmap.sh has no write-up for this node yet."
                    : "");
                root.detailResources = res.resources || [];
            }
        }
    }

    Process { id: openLinkProc }

    Process {
        id: progressLoad
        command: ["sh", "-c", "cat '" + root.progressPath + "' 2>/dev/null || echo '{}'"]
        running: false
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    var parsed = JSON.parse(String(text).trim() || "{}");
                    if (parsed && typeof parsed === "object") {
                        // New shape is {done, recent}; anything older is the
                        // bare done-map and still loads.
                        if (parsed.done !== undefined) {
                            root.doneMap = parsed.done || ({});
                            root.recent = parsed.recent || ({});
                            root.totals = parsed.totals || ({});
                        } else {
                            root.doneMap = parsed;
                        }
                    }
                } catch (e) { root.doneMap = ({}); }
            }
        }
    }

    Process { id: progressSave }

    // Ticking several boxes in a row is one write, not one per click.
    Timer {
        id: saveTimer
        interval: 700
        onTriggered: {
            // Empty entries are dropped: a roadmap you ticked something in and
            // then unticked would otherwise count as "has progress" forever.
            var pruned = {};
            for (var slug in root.doneMap) {
                var ids = root.doneMap[slug], any = false;
                for (var k in ids) if (ids[k]) { any = true; break; }
                if (any) pruned[slug] = ids;
            }
            root.doneMap = pruned;
            var payload = JSON.stringify({ done: pruned, recent: root.recent,
                                           totals: root.totals });
            progressSave.command = ["sh", "-c",
                "mkdir -p \"$(dirname '" + root.progressPath + "')\" && " +
                "printf '%s' \"$1\" > '" + root.progressPath + ".tmp' && " +
                "mv '" + root.progressPath + ".tmp' '" + root.progressPath + "'",
                "--", payload];
            progressSave.running = false;
            progressSave.running = true;
        }
    }

    // ---------------- search + progress ----------------
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.scaled(8)

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(32)
            radius: Theme.bubbleRadiusSmall
            color: "transparent"
            border.color: searchInput.activeFocus ? Theme.accentColor : Theme.glassBorder
            border.width: 1

            TextInput {
                id: searchInput
                anchors.fill: parent
                anchors.leftMargin: Theme.scaled(10)
                anchors.rightMargin: Theme.scaled(10)
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.text
                font.pixelSize: Theme.scaled(11)
                selectByMouse: true
                clip: true
                onTextChanged: { root.searchText = text; root.applyFilter(); }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Search " + (root.listLoading ? "roadmaps…" : allModel.count + " roadmaps")
                    color: Theme.subtext0
                    font.pixelSize: Theme.scaled(11)
                    visible: searchInput.text === ""
                }
            }
        }

        Repeater {
            model: [["-", -0.2], ["+", 0.25]]
            delegate: Rectangle {
                required property var modelData
                width: Theme.scaled(22); height: Theme.scaled(22)
                radius: Theme.scaled(6)
                color: zoomMouse.containsMouse ? Theme.cardBackgroundHover : Theme.cardBackground
                border.width: 0
                Text {
                    anchors.centerIn: parent
                    text: modelData[0]
                    color: Theme.subtext1
                    font.pixelSize: Theme.scaled(13)
                    font.weight: Font.Bold
                }
                MouseArea {
                    id: zoomMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: graphView.zoomBy(modelData[1])
                }
            }
        }

        Rectangle {
            width: Theme.scaled(30); height: Theme.scaled(22)
            radius: Theme.scaled(6)
            color: fitMouse.containsMouse ? Theme.cardBackgroundHover : Theme.cardBackground
            border.width: 0
            Text {
                anchors.centerIn: parent
                text: "fit"
                color: Theme.subtext1
                font.pixelSize: Theme.scaled(9)
                font.weight: Font.Bold
            }
            MouseArea {
                id: fitMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: graphView.resetZoom()
            }
        }
    }

    // ---------------- roadmap picker ----------------
    ListView {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.scaled(26)
        orientation: ListView.Horizontal
        spacing: Theme.scaled(5)
        clip: true
        model: filteredModel
        visible: filteredModel.count > 0

        delegate: Rectangle {
            id: tabItem
            required property string slug
            required property string title
            readonly property bool isActive: slug === root.activeSlug

            width: tabLabel.implicitWidth + Theme.scaled(18)
            height: Theme.scaled(23)
            radius: height / 2
            color: isActive ? Theme.accentColor
                            : (tabMouse.containsMouse ? Theme.cardBackgroundHover : "transparent")
            border.width: 0
            Behavior on color { ColorAnimation { duration: Theme.animInstant } }

            readonly property int chipDone: root.doneFor(slug)
            readonly property int chipTotal: root.totalFor(slug)
            readonly property real chipFraction: chipTotal > 0
                ? Math.min(1, chipDone / chipTotal) : 0

            Row {
                id: tabLabel
                anchors.centerIn: parent
                spacing: Theme.scaled(4)

                Text {
                    text: tabItem.title
                    color: tabItem.isActive ? Theme.base : Theme.subtext1
                    font.pixelSize: Theme.scaled(10)
                    font.weight: tabItem.isActive ? Font.Bold : Font.Medium
                    anchors.verticalCenter: parent.verticalCenter
                }
                // The count is the point: 102 nodes deep into a roadmap should
                // not look identical to one never opened.
                Text {
                    visible: tabItem.chipDone > 0
                    text: tabItem.chipTotal > 0
                          ? tabItem.chipDone + "/" + tabItem.chipTotal
                          : String(tabItem.chipDone)
                    color: tabItem.isActive ? Theme.base : Theme.accentColor
                    opacity: tabItem.isActive ? 0.75 : 1.0
                    font.pixelSize: Theme.scaled(8)
                    font.weight: Font.Black
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Progress as a hairline along the bottom of the chip.
            Rectangle {
                visible: tabItem.chipFraction > 0
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.scaled(6)
                anchors.bottomMargin: Theme.scaled(3)
                width: (tabItem.width - Theme.scaled(12)) * tabItem.chipFraction
                height: Theme.scaled(2)
                radius: height / 2
                color: tabItem.isActive ? Theme.base : Theme.accentColor
                opacity: tabItem.isActive ? 0.65 : 0.85
                Behavior on width { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
            }
            MouseArea {
                id: tabMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.selectSlug(tabItem.slug, tabItem.title)
            }
        }
    }

    // ---------------- title + legend ----------------
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.scaled(8)
        visible: root.activeSlug !== ""

        Text {
            text: root.activeTitle
            color: Theme.accentColor
            font.pixelSize: Theme.scaled(14)
            font.weight: Font.Black
            elide: Text.ElideNone
            Layout.fillWidth: true
        }
        Text {
            text: root.togglableCount > 0
                  ? root.doneCount + " / " + root.togglableCount + " done"
                  : ""
            color: Theme.subtext0
            font.pixelSize: Theme.scaled(10)
            font.weight: Font.Bold
        }
        Item { Layout.fillWidth: true }
    }

    Flow {
        Layout.fillWidth: true
        spacing: Theme.scaled(10)
        visible: legendModel.count > 1

        Repeater {
            model: legendModel
            delegate: Row {
                id: legendRow
                required property string ltext
                required property string lswatch
                spacing: Theme.scaled(4)

                Rectangle {
                    width: Theme.scaled(8); height: Theme.scaled(8)
                    radius: 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: "transparent"
                    border.color: legendRow.lswatch
                    border.width: 2
                }
                Text {
                    text: legendRow.ltext
                    color: Theme.subtext0
                    font.pixelSize: Theme.scaled(9)
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    // ---------------- the roadmap itself ----------------
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: Theme.bubbleRadiusMedium
        color: Theme.cardBackground
        border.color: Theme.glassBorder
        border.width: 1
        clip: true

        Flickable {
            id: graphView
            anchors.fill: parent
            anchors.margins: Theme.scaled(6)
            contentWidth: root.graphW * viewScale
            contentHeight: root.graphH * viewScale
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            visible: nodeModel.count > 0

            property real viewScale: 1.0
            // Set once the user touches the zoom buttons, so their choice is
            // not thrown away by a later resize.
            property bool userZoomed: false

            // Exact fill, with no upper clamp: the previous 1.6 cap combined
            // with a "only refit while viewScale is still 1.0" guard meant the
            // graph fitted itself to whatever width the panel happened to have
            // before layout settled, and then never corrected -- which is why
            // it sat in a narrow column with the panel half empty.
            function fitToWidth() {
                if (root.graphW <= 0 || width <= 0) return;
                viewScale = Math.max(0.1, Math.min(4.0, width / root.graphW));
            }
            function zoomBy(delta) {
                userZoomed = true;
                viewScale = Math.max(0.15, Math.min(4.0, viewScale * (1.0 + delta)));
            }
            function resetZoom() {
                userZoomed = false;
                fitToWidth();
                contentX = 0;
            }
            onWidthChanged: if (!userZoomed) fitToWidth()

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            FastWheel {}

            Item {
                width: root.graphW
                height: root.graphH
                transform: Scale {
                    xScale: graphView.viewScale
                    yScale: graphView.viewScale
                }

                // Connectors, drawn as vector paths so they stay crisp at any
                // zoom instead of being rasterised into a canvas the size of the
                // whole roadmap (over three thousand pixels tall).
                //
                // One Shape per edge because Repeater only accepts Item
                // delegates -- a ShapePath is not an Item, so generating them
                // directly inside a single Shape would produce nothing.
                Repeater {
                    model: root.edgeList
                    delegate: Shape {
                        required property var modelData
                        anchors.fill: parent
                        z: 0
                        preferredRendererType: Shape.CurveRenderer

                        readonly property real cd: Math.min(70, Math.max(24,
                              Math.abs(modelData.tx - modelData.sx) / 2
                            + Math.abs(modelData.ty - modelData.sy) / 2))

                        function offX(d) { return d === "y" ? -cd : (d === "z" ? cd : 0); }
                        function offY(d) { return d === "w" ? -cd : (d === "x" ? cd : 0); }

                        ShapePath {
                            strokeColor: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g,
                                                 Theme.accentColor.b, 0.45)
                            strokeWidth: 2
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            strokeStyle: modelData.dashed ? ShapePath.DashLine : ShapePath.SolidLine
                            dashPattern: [2, 3]
                            startX: modelData.sx
                            startY: modelData.sy
                            PathCubic {
                                x: modelData.tx
                                y: modelData.ty
                                control1X: modelData.sx + offX(modelData.sd)
                                control1Y: modelData.sy + offY(modelData.sd)
                                control2X: modelData.tx + offX(modelData.td)
                                control2Y: modelData.ty + offY(modelData.td)
                            }
                        }
                    }
                }

                Repeater {
                    model: nodeModel
                    delegate: Rectangle {
                        id: nodeBox
                        required property int index
                        required property string id
                        required property string type
                        required property int nx
                        required property int ny
                        required property int nw
                        required property int nh
                        required property string label
                        required property int fs
                        required property string lg
                        required property string lc
                        required property bool done

                        readonly property color accent: lc !== "" ? lc : Theme.accentColor

                        readonly property bool togglable: root.isTogglable(type)

                        x: nx; y: ny; width: nw; height: nh
                        // Sections are the backdrop panels the website groups
                        // nodes into, so they sit behind everything else.
                        // section < paragraph < everything else. Both are
                        // containers that other nodes are drawn inside, so
                        // leaving them on the same layer as their contents left
                        // paint order to decide which covered which.
                        z: type === "section" ? -2 : (type === "paragraph" ? -1 : 1)
                        radius: type === "section" ? 10 : 6

                        // Solid, not translucent: these sit over a glass card,
                        // and a see-through box made the label compete with
                        // whatever was behind the panel.
                        color: type === "section" ? Qt.rgba(1, 1, 1, 0.03)
                             : type === "paragraph" ? "transparent"
                             : type === "label"   ? "transparent"
                             : done               ? accent
                             : type === "topic"   ? Qt.darker(Theme.surfaceContainerHigh, 1.05)
                             : type === "button"  ? Qt.rgba(0, 0, 0, 0.55)
                                                  : Qt.darker(Theme.surfaceContainerHigh, 1.25)

                        border.width: (type === "label" || type === "section" || type === "paragraph")
                                      ? 0 : (type === "topic" ? 2 : 1)
                        // Border colour is the roadmap.sh legend colour, so a
                        // glance tells you core from optional.
                        border.color: done ? accent
                                   : lc !== "" ? accent
                                   : type === "topic" ? Theme.accentColor
                                                      : Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.4)

                        opacity: nodeMouse.containsMouse && togglable ? 0.85 : 1.0
                        Behavior on color { ColorAnimation { duration: Theme.animInstant } }

                        Text {
                            anchors.fill: parent
                            anchors.margins: 3
                            // Paragraph nodes are containers, not labels: the
                            // heading sits at the top of the box and the buttons
                            // roadmap.sh draws underneath live inside the same
                            // rectangle. Centring the text vertically put it
                            // straight through those buttons.
                            anchors.topMargin: nodeBox.type === "paragraph" ? 6 : 3
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: nodeBox.type === "paragraph"
                                             ? Text.AlignTop : Text.AlignVCenter
                            wrapMode: Text.WordWrap
                            // Never truncate. The boxes are fixed sizes handed
                            // down by the API, so the text shrinks to fit them
                            // instead -- "API Au..." tells you nothing, and
                            // letting it spill would run into the next node.
                            elide: Text.ElideNone
                            // Fit both dimensions, and wrap. HorizontalFit was
                            // tried for containers and is wrong next to
                            // WordWrap: it shrinks until the text fits the width
                            // on one line rather than letting it wrap, so 57
                            // characters in a 280px box collapsed to a few
                            // pixels tall.
                            fontSizeMode: Text.Fit
                            // Headings and captions get a floor. They are the
                            // labels most likely to sit in a box that is tight
                            // for their length, and shrinking them to nothing is
                            // worse than letting them wrap onto another line.
                            minimumPixelSize: (nodeBox.type === "paragraph"
                                               || nodeBox.type === "label") ? 9 : 5
                            text: nodeBox.label
                            visible: nodeBox.label !== ""
                            color: nodeBox.done ? Theme.base
                                 : nodeBox.type === "topic" ? Theme.text
                                 : (nodeBox.type === "label" || nodeBox.type === "paragraph")
                                   ? Theme.subtext0
                                                            : Theme.subtext1
                            // Captions ("Learn the Pre-requisites", "Examples
                            // of Tools") are section headings, so they get a
                            // floor rather than whatever the API happened to
                            // set -- they were the smallest text on screen.
                            font.pixelSize: {
                                var base = nodeBox.fs > 0 ? nodeBox.fs : 14;
                                if (nodeBox.type === "label" || nodeBox.type === "paragraph")
                                    return Math.max(base, 16);
                                return base;
                            }
                            font.weight: nodeBox.type === "topic"     ? Font.Bold
                                       : nodeBox.type === "label"     ? Font.DemiBold
                                       : nodeBox.type === "paragraph" ? Font.DemiBold
                                                                      : Font.Normal
                            font.strikeout: nodeBox.done
                        }

                        // Completion badge, as on roadmap.sh: a tick on the
                        // corner rather than relying on the fill colour alone,
                        // which is hard to read at fit-to-width zoom.
                        Rectangle {
                            visible: nodeBox.done && nodeBox.togglable
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.rightMargin: -6
                            width: 16; height: 16
                            radius: 8
                            color: nodeBox.accent
                            border.color: Theme.base
                            border.width: 2
                            z: 3
                            Text {
                                anchors.centerIn: parent
                                text: "\u2713"
                                color: Theme.base
                                font.pixelSize: 10
                                font.weight: Font.Black
                            }
                        }

                        MouseArea {
                            id: nodeMouse
                            anchors.fill: parent
                            enabled: nodeBox.togglable
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.openDetail(nodeBox.index)
                        }
                    }
                }
            }
        }

        // ---------------- topic detail ----------------
        RoadmapDetail {
            anchors.fill: parent
            visible: root.detailId !== ""
            z: 5

            topicTitle: root.detailTitle
            body: root.detailBody
            resources: root.detailResources
            loading: root.detailLoading
            isDone: root.detailDone

            onClosed: root.closeDetail()
            onToggleDone: root.toggleDetailDone()
            onOpenLink: (url) => {
                openLinkProc.command = ["xdg-open", url];
                openLinkProc.running = false;
                openLinkProc.running = true;
            }
        }

        Text {
            anchors.centerIn: parent
            width: parent.width - Theme.scaled(40)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.subtext0
            font.pixelSize: Theme.scaled(11)
            visible: nodeModel.count === 0
            text: root.listLoading            ? "Loading roadmaps…"
                : root.listFailed             ? "Could not reach GitHub. The list is cached once it loads."
                : filteredModel.count === 0   ? "No roadmap matches “" + root.searchText + "”"
                : root.graphLoading           ? "Loading " + root.activeTitle + "…"
                : root.activeSlug === ""      ? "Pick a roadmap above"
                                              : "No roadmap data published for " + root.activeTitle
        }
    }
}
