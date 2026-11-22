.pragma library
// Linux distro looks for Aether (Mix & match ids, e.g. windows.json "mixBar": "debian").
// family = the desktop the distro ships (gnome, kde, cinnamon, xfce, pantheon, cosmic, budgie); the pieces are
// built once per family and themed per distro. Plan and status: dotfiles/DISTRO-LOOKS-PLAN.md
var distros = [
    { id: "debian", name: "Debian", family: "gnome", accent: "#3584e4", brand: "#d70a53",
      wallpaper: "debian/ceratopsian-1920x1080.svg",
      // Debian's pinned apps in its order (debian/gnome-shell.gsettings-override): Firefox ESR, Evolution, Files,
      // Software, Text Editor, Calculator, Help. Each role = the Windows apps that fill it, the first one found wins.
      favourites: [["firefox", "zen"], ["olk", "outlook", "thunderbird", "hxoutlook"], ["aether-files", "explorer"],
                   ["winstore.app", "store", "microsoft store"], ["notepad", "texteditor"], ["calculatorapp", "calculator", "calc"],
                   ["gethelp", "get help", "tips"]],
      blurb: "GNOME as Debian ships it: black top bar, workspace dots, calendar + quick settings menus" }
];

// the favourite keys to show: each role's first key that `has(key)` (installed or running), in order
function favKeys(distro, has) {
    var out = [];
    var favs = (distro && distro.favourites) || [];
    for (var i = 0; i < favs.length; i++) {
        var role = typeof favs[i] === "string" ? [favs[i]] : favs[i];
        for (var j = 0; j < role.length; j++)
            if (has(role[j])) {
                out.push(role[j]);
                break;
            }
    }
    return out;
}

var ids = distros.map(function (d) { return d.id; });

function get(id) {
    for (var i = 0; i < distros.length; i++)
        if (distros[i].id === id)
            return distros[i];
    return null;
}
