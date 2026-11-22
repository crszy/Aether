pragma ComponentBehavior: Bound
// One part of the desktop: which shell draws it, then every option every shell has for it.
import QtQuick
import qs.services as Cae
import qs.utils as CU
import "catalog.js" as Cat

PageScroll {
    id: page

    property var win: null
    property string pieceId: ""
    // serpantinum and DarkShadowShell keep their settings on their own pages: link there when one draws this part
    readonly property string cur: piece && piece.mix ? String(Cae.WinSettings[piece.mix]) : ""
    readonly property bool ownPage: (cur === "dss" || cur === "serp" || cur === "aether") && !(piece && piece.options.some(o => o.shell === cur))
    property string query: ""
    readonly property var piece: Cat.pieces[pieceId] || null
    readonly property string cae: CU.Paths.config
    readonly property string noc: CU.Paths.home + "/.config/noctalia/settings.json"

    // the shell in use first, so in Simple mode its options are the ones at the top
    readonly property var ordered: {
        if (!piece)
            return [];
        const cur = piece.mix ? String(Cae.WinSettings[piece.mix]) : "";
        const opts = piece.options.slice();
        return opts.filter(o => o.shell === cur).concat(opts.filter(o => o.shell !== cur));
    }

    function sourcePath(o) {
        return o.shell === "noctalia" ? noc : cae + "/" + o.file;
    }

    T {
        kind: "reading"
        width: parent.width
        text: page.piece ? page.piece.desc : ""
        color: M3.inkDim
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }

    SectionTitle {
        visible: !!(page.piece && page.piece.mix)
        text: page.piece && page.piece.choices ? "Mode" : "Drawn by"
        sub: page.piece && page.piece.choices ? "" : "Click a shell to use its version of this part. You can switch back any time."
    }
    ShellPicker {
        visible: !!(page.piece && page.piece.mix)
        piece: page.piece
    }

    Item {
        width: parent.width
        height: optTitle.height
        SectionTitle {
            id: optTitle
            width: parent.width - modeSeg.width - 16
            text: "Options"
            sub: Prefs.expert ? "Every option of every shell, read from and written to each shell's own file. Changes are live."
                : "The shell you use comes first; the others are folded. Changes are live. Pick All options to see file paths and every setting."
        }
        Seg {
            id: modeSeg
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            options: [{ value: "simple", label: "Simple" }, { value: "all", label: "All options" }]
            current: Prefs.expert ? "all" : "simple"
            onChosen: v => Prefs.setExpert(v === "all")
        }
    }
    Rectangle {
        visible: page.ownPage
        width: parent.width
        height: 76
        radius: M3.rL
        color: M3.secondaryContainer
        ShellLogo {
            id: ownLogo
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            size: 44
            shell: page.cur
            colour: Cat.shell(page.cur).colour
            icon: page.cur === "dss" ? "dark_mode" : page.cur === "aether" ? "auto_awesome" : "widgets"
        }
        T {
            anchors.left: ownLogo.right
            anchors.leftMargin: 14
            anchors.right: ownBtn.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: Cat.shell(page.cur).name + " draws this now. Its settings have their own page."
            color: M3.inkSecondaryC
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }
        Btn {
            id: ownBtn
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            variant: "filled"
            text: "Open its settings"
            sym: "arrow_forward"
            onClicked: page.win && page.win.go(page.cur, page.cur === "aether" ? (page.pieceId === "dock" ? "aetherdock" : "aetherbar") : page.cur === "dss" && page.pieceId === "dock" ? "dssdock" : "")
        }
    }
    Repeater {
        model: page.ordered
        JsonSection {
            required property var modelData
            title: modelData.title || (Cat.shell(modelData.shell).name + " · " + page.piece.name.toLowerCase())
            path: page.sourcePath(modelData)
            section: modelData.section || ""
            include: modelData.include || ""
            exclude: modelData.exclude || "pinHash|pinSalt"
            choices: modelData.choices || ({})
            multiChoices: modelData.multiChoices || ({})
            query: page.query
            accent: Cat.shell(modelData.shell).colour
            active: !!page.piece.mix && Cae.WinSettings[page.piece.mix] === modelData.shell
        }
    }
}
