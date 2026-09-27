// src/config/Tokens.h  —  Aether shell
// Native port of Caelestia's design tokens.
// Source of truth: caelestia-dots/shell  plugin/src/Caelestia/Config/tokens.hpp
// Aether is a GPL-3.0 derivative of Caelestia Shell. Values here are the EXACT defaults.
#pragma once
// Windows <rpcndr.h> does `#define small char` / `#define hyper __int64` — kill them so our
// token member named `small` (matching Caelestia) is a real identifier.
#ifdef small
#undef small
#endif
#ifdef hyper
#undef hyper
#endif

namespace Tok {
    // RoundingTokens / SpacingTokens / PaddingTokens all share this scale
    namespace rounding { enum { extraSmall=4, small=8, medium=12, large=16, largeIncreased=20,
                                extraLarge=28, extraLargeIncreased=32, extraExtraLarge=48, full=100000 }; }
    namespace spacing  { enum { extraSmall=4, small=8, medium=12, large=16, largeIncreased=20,
                                extraLarge=28, extraLargeIncreased=32, extraExtraLarge=48 }; }
    namespace padding  { enum { extraSmall=4, small=8, medium=12, large=16, largeIncreased=20,
                                extraLarge=28, extraLargeIncreased=32, extraExtraLarge=48 }; }
    // FontSizeTokens (pt in Caelestia; used as relative scale here)
    namespace font     { enum { small=11, smaller=12, normal=13, larger=15, large=18, extraLarge=28 }; }
    // BarTokens
    namespace bar      { enum { innerWidth=40, windowPreviewSize=400, trayMenuWidth=300,
                                batteryWidth=250, networkWidth=320, kbLayoutWidth=320 }; }
    // DashboardTokens (subset used by the drawer)
    namespace dash     { enum { tabIndicatorHeight=3, tabIndicatorSpacing=5, userWidth=340, logoSize=30,
                                uptimeSize=30, dateTimeWidth=110, mediaWidth=200, mediaProgressSweep=180,
                                mediaProgressThickness=6, resourceProgressThickness=6, weatherWidth=275,
                                mediaCoverArtSize=200, mediaTabWidth=1000, mediaTabHeight=320, mediaSectionWidth=300,
                                perfHeroCardWidth=400, perfUsageShapeSize=100, perfNetworkCardWidth=390,
                                perfNetworkCardHeight=220, perfBattWidth=150, perfPlaceholderWidth=700 }; }
    // LauncherTokens
    namespace launcher { enum { itemWidth=600, itemHeight=57, wallpaperWidth=280, wallpaperHeight=200 }; }
    // NotifsTokens
    namespace notifs   { enum { width=430, image=42, badge=20 }; }
}
