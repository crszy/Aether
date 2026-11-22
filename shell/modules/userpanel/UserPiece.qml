// One of your own parts from ~/.config/caelestia/user/<kind>/<name>.qml, loaded wherever Aether puts it
// (bar widget, panel, lock screen, lock clock/media slot, OSD, dashboard tab, desktop widget).
//   context   values handed to the file: each key is set on the loaded item if it declares that property
//             (e.g. { pam: ..., screen: ... } -> `property var pam` in your file gets the lock's password state)
//   fill      true: the file fills this item (lock screens); false: it sizes itself
// The file's text is read here and built with Qt.createQmlObject: Qt's own file loader remembers each folder's
// listing, so a file made while Aether runs was refused ("File name case mismatch") until a restart.
// Saving the file rebuilds it. A file with a mistake shows its error (line:column: message) instead.
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Caelestia.Config
import qs.components
import qs.services

Item {
    id: root

    property string kind: ""
    property string name: ""
    property var context: ({})
    property bool fill: false

    property string error: ""
    property Item item: null
    readonly property bool ready: item !== null
    readonly property string path: kind && UserExt.validName(name) ? UserExt.filePath(kind, name) : ""

    implicitWidth: error !== "" ? errBox.implicitWidth : (item?.implicitWidth ?? 0)
    implicitHeight: error !== "" ? errBox.implicitHeight : (item?.implicitHeight ?? 0)

    function clear(): void {
        if (root.item) {
            root.item.destroy();
            root.item = null;
        }
    }

    function build(text: string): void {
        root.clear();
        let obj = null;
        try {
            obj = Qt.createQmlObject(text, holder, `file:///${root.path}`);
        } catch (e) {
            const errs = e?.qmlErrors ?? [];
            root.error = errs.length ? errs.map(er => `line ${er.lineNumber}:${er.columnNumber}: ${er.message}`).join("\n") : String(e);
            console.warn("[user]", root.kind, root.name, root.error);
            return;
        }
        if (!obj || typeof obj.implicitWidth !== "number" || !("anchors" in obj)) {
            obj?.destroy();
            root.error = "the file's outermost type has to be a visual item (Item, Rectangle, ColumnLayout ...)";
            return;
        }
        root.error = "";
        if (root.fill)
            obj.anchors.fill = holder;
        else
            obj.anchors.centerIn = holder;
        root.item = obj;
        root.applyContext();
    }

    function applyContext(): void {
        const it = root.item;
        if (!it || !root.context)
            return;
        for (const k of Object.keys(root.context))
            if (k in it)
                it[k] = root.context[k];
    }

    onContextChanged: applyContext()
    onPathChanged: {
        root.error = "";
        if (!root.path)
            root.clear();
    }
    Component.onDestruction: root.clear()

    FileView {
        id: file

        path: root.path
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.build(text())
        onLoadFailed: {
            root.clear();
            if (root.path)
                root.error = "file not found";
        }
    }

    Item {
        id: holder

        anchors.fill: parent
    }

    // a mistake in the file: what and where
    StyledRect {
        id: errBox

        visible: root.error !== ""
        anchors.centerIn: parent
        implicitWidth: Math.min(520, errText.implicitWidth + 64)
        implicitHeight: errRow.implicitHeight + 24
        radius: Tokens.rounding.medium
        color: AetherStyle.palette.m3errorContainer

        RowLayout {
            id: errRow

            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            MaterialIcon {
                Layout.alignment: Qt.AlignTop
                text: "error"
                color: AetherStyle.palette.m3onErrorContainer
            }

            StyledText {
                id: errText

                Layout.fillWidth: true
                text: `${root.kind}/${root.name}.qml\n${root.error}`
                wrapMode: Text.Wrap
                color: AetherStyle.palette.m3onErrorContainer
                font.pointSize: 9
            }
        }
    }
}
