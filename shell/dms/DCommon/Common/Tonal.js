.pragma library
.import "Contrast.js" as Contrast
.import "Hct.js" as Hct

var CONTAINER_TINT_DARK = 0.5;
var CONTAINER_TINT_LIGHT = 1;
var CONTAINER_CHROMA_MAX = 48;
var DARK_TONE_LIMIT = 50;
var DARK_TONE_FLOOR = 30;
var DARK_TONE_LIFT = 16;
var LIGHT_TONE_CEILING = 90;
var LIGHT_TONE_DROP = 4;

function defaultTint(card) {
    return Hct.toHct(card).tone < DARK_TONE_LIMIT ? CONTAINER_TINT_DARK : CONTAINER_TINT_LIGHT;
}

// Fading toward the card's own hue, not grey: a grey-ward purple reads red on a blue-tinted surface.
function softContainer(accent, card, tint) {
    const source = Hct.toHct(accent);
    const surface = Hct.toHct(card);
    const tone = surface.tone < DARK_TONE_LIMIT ? Math.max(DARK_TONE_FLOOR, surface.tone + DARK_TONE_LIFT) : Math.min(LIGHT_TONE_CEILING, surface.tone - LIGHT_TONE_DROP);
    const base = Hct.fromHct(surface.hue, surface.chroma, tone);
    const full = Hct.fromHct(source.hue, Math.min(source.chroma, CONTAINER_CHROMA_MAX), tone);
    return Contrast.mix(base, full, Math.max(0, Math.min(1, tint)));
}
