// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
import QtQuick

Grid {
    property bool vertical: false

    flow: Grid.LeftToRight
    rows: vertical ? -1 : 1
    columns: vertical ? 1 : -1

    onVerticalChanged: {
        rows = -1;
        columns = -1;
        if (vertical) columns = 1;
        else rows = 1;
    }
}
