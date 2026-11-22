// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
.pragma library

function nestLevel(item, stopAt, countBackdrops) {
    let count = 0;
    let a = item.parent;
    let depth = 0;
    while (a && a !== stopAt && depth < 48) {
        const kids = a.children;
        if (kids) {
            for (let i = 0; i < kids.length; i++) {
                const k = kids[i];
                if (k && k !== item && ((countBackdrops && k.isGlassBackdrop === true && k.standalone !== true) || (k.isGlassFill === true && k.allowed === true) || (k.item && k.item !== item && k.item.isGlassFill === true && k.item.allowed === true))) {
                    count++;
                    break;
                }
            }
        }
        a = a.parent;
        depth++;
    }
    return count;
}
