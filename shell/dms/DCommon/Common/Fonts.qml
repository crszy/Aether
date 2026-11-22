pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell

Singleton {
    readonly property string sans: googleSansFont.name || "Google Sans Flex"
    readonly property string mono: firaCodeFont.name || "Fira Code"
    readonly property string icons: materialSymbolsFont.name || "Material Symbols Rounded"
    readonly property string nerd: firaCodeFont.name || "FiraCode Nerd Font"
    readonly property string display: dmSerifDisplayFont.name || "DM Serif Display"
    readonly property string notable: notableFont.name || "Notable"
    readonly property string gochiHand: gochiHandFont.name || "Gochi Hand"
    readonly property var bundledFamilies: [display, notable, gochiHand]
    readonly property string system: Qt.application.font.family
    readonly property string systemMono: "monospace"

    function widestDigit(font) {
        metrics.font = font;
        let widest = "0";
        let widestAdvance = 0;
        for (const digit of "0123456789") {
            const advance = metrics.advanceWidth(digit);
            if (advance <= widestAdvance)
                continue;
            widestAdvance = advance;
            widest = digit;
        }
        return widest;
    }

    // QTextLayout adds the last glyph's ink overhang past its cached integer advance (qtextlayout.cpp negativeRightBearing)
    function textWidth(font, text) {
        if (text === "")
            return 0;
        metrics.font = font;
        const last = text.slice(-1);
        const trailing = /\d/.test(last) ? "0123456789" : last;
        let overhang = 0;
        for (const glyph of trailing) {
            const box = metrics.boundingRect(glyph);
            overhang = Math.max(overhang, box.x + box.width - Math.round(metrics.advanceWidth(glyph)));
        }
        return metrics.advanceWidth(text) + overhang;
    }

    function selectableFamilies(defaultFamily) {
        const bundled = bundledFamilies.filter(f => f !== defaultFamily);
        const installed = Qt.fontFamilies().filter(f => !f.startsWith(".") && f !== defaultFamily && !bundled.includes(f));
        installed.sort();
        return bundled.concat(installed);
    }

    FontMetrics {
        id: metrics
    }

    FontLoader {
        id: googleSansFont
        source: Qt.resolvedUrl("../assets/fonts/google-sans-flex/GoogleSansFlex.ttf")
    }

    Instantiator {
        model: [100, 200, 300, 500, 600, 700, 800, 900]
        delegate: FontLoader {
            required property int modelData
            source: Qt.resolvedUrl(`../assets/fonts/google-sans-flex/GoogleSansFlex-${modelData}.ttf`)
        }
    }

    FontLoader {
        id: firaCodeFont
        source: Qt.resolvedUrl("../assets/fonts/nerd-fonts/FiraCodeNerdFont-Regular.ttf")
    }

    FontLoader {
        id: dmSerifDisplayFont
        source: Qt.resolvedUrl("../assets/fonts/dm-serif-display/DMSerifDisplay-Regular.ttf")
    }

    FontLoader {
        id: notableFont
        source: Qt.resolvedUrl("../assets/fonts/notable/Notable-Regular.ttf")
    }

    FontLoader {
        id: gochiHandFont
        source: Qt.resolvedUrl("../assets/fonts/gochi-hand/GochiHand-Regular.ttf")
    }

    FontLoader {
        id: materialSymbolsFont
        source: Qt.resolvedUrl("../assets/fonts/material-design-icons/variablefont/MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf")
    }
}
