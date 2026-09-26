// src/modules/bar/components/Clock.h  —  Aether shell
// Native port of Caelestia's bar clock.
// Source of truth: caelestia-dots/shell  modules/bar/components/Clock.qml
// Aether is a GPL-3.0 derivative of Caelestia Shell.
//
// Clock.qml, read literally:
//   readonly property color colour: Colours.palette.m3tertiary
//   ColumnLayout { spacing: Tokens.spacing.extraSmall }
//     Loader (active: Config.bar.clock.showIcon, DEFAULT TRUE) -> MaterialIcon "calendar_month"
//     StyledText  Time.hourStr
//     StyledText  Time.minuteStr   Layout.topMargin: -parent.spacing - 4
//     Loader (12-hour only)        Layout.topMargin: -parent.spacing - 4
//                                  text: Time.amPmStr.toLowerCase(), font scale 0.9
// So: an accent-tinted calendar glyph, then hour over minute pulled TIGHT (the negative margin
// cancels the 4px column spacing and takes another 4 off, i.e. the two lines nearly touch), then a
// small lower-case meridiem. Config.bar.clock.background defaults to FALSE - there is no pill.
//
// This file previously drew three equal white lines at a 23px pitch with no icon. That is a
// different component; the reference frames show the tinted calendar mark and the tight stack.
#pragma once

// One clock row: the hour/minute glyph pitch. Upstream's negative margin makes the second line sit
// a shade under one line height, so this is deliberately tighter than the 21px font's natural lead.
static const float BARCLOCK_FS   = 21.0f;   // Tokens.font.body.small scaled 1.1
static const float BARCLOCK_ROW  = 19.0f;
static const float BARCLOCK_APFS = 15.0f;   // meridiem: the same builder at scale 0.9
static const float BARCLOCK_ICON = 20.0f;   // calendar_month cell

// Total implicit height, so the bar layout can place it like any other entry.
// withIcon mirrors Config.bar.clock.showIcon (Aether turns it off when the user has the separate
// calendar entry enabled, so the strip never carries two calendar glyphs).
static float BarClockH(bool withIcon){
    float h = 0.0f;
    if(withIcon) h += BARCLOCK_ICON + (float)Tok::spacing::extraSmall;
    h += BARCLOCK_ROW * 2.0f;                       // hour + minute
    if(!g_clock24)  h += BARCLOCK_APFS;             // am/pm
    return h;
}

// cx = strip centre, ox = per-item entrance x-offset, ckT = TOP of the clock's run,
// entrance = 0..1 stagger alpha, withIcon = Config.bar.clock.showIcon
static void DrawBarClock(ImDrawList* dl, float cx, float ox, float ckT, float entrance, bool withIcon){
    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
    int h12 = g_clock24 ? lt.tm_hour : (((lt.tm_hour%12)==0) ? 12 : lt.tm_hour%12);
    char hh[8], mm[8]; snprintf(hh,8,"%02d",h12); snprintf(mm,8,"%02d",lt.tm_min);
    // Time.amPmStr.toLowerCase() - upstream renders the meridiem in lower case, and at this size the
    // difference between "PM" and "pm" is most of the component's character.
    const char* ap = g_clock24 ? "" : (lt.tm_hour<12 ? "am" : "pm");
    auto IA=[&](ImU32 c){ return MulA(c,entrance); };

    const ImU32 tert = M3Tertiary();                // Colours.palette.m3tertiary
    float y = ckT;
    if(withIcon){
        CalIcon(dl, V(cx+ox, y+BARCLOCK_ICON*0.5f), IA(tert));
        y += BARCLOCK_ICON + (float)Tok::spacing::extraSmall;
    }
    // -3 pulls the glyphs up inside their line box so the pair reads as one block, which is what the
    // negative Layout.topMargin does upstream.
    TextAt(dl,g_fMed,BARCLOCK_FS,V(cx+ox-TextW(g_fMed,BARCLOCK_FS,hh)*0.5f, y-3.0f), IA(tert), hh);
    y += BARCLOCK_ROW;
    TextAt(dl,g_fMed,BARCLOCK_FS,V(cx+ox-TextW(g_fMed,BARCLOCK_FS,mm)*0.5f, y-3.0f), IA(tert), mm);
    y += BARCLOCK_ROW;
    if(*ap) TextAt(dl,g_fMed,BARCLOCK_APFS,
                   V(cx+ox-TextW(g_fMed,BARCLOCK_APFS,ap)*0.5f, y-2.0f), IA(WithA(tert,225)), ap);
}
