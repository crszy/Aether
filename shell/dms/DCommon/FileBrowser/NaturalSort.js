.pragma library

// Qt's localeCompare ignores Intl options such as numeric, so names compare by this key instead.
// Zero-padding digit runs matches padDigits in dux-go files/sort.go, so search results sort like directory listings.
const digitPad = 20;

function key(name) {
    return name.toLowerCase().replace(/\d+/g, run => run.padStart(digitPad, "0"));
}
