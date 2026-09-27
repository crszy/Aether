# Changelog

## v1.6.2 - 2026-09-26

### Fixed

-   **The Performance tab did not load.** A built-in tab could be left with no widgets (the page is one big widget,
    and its delete ✕ in edit mode emptied the tab), which drew an empty panel. A stock tab with nothing on it now
    gets its page back when Aether starts, and a custom tab that is empty says so instead of showing nothing.
-   **Lyrics could belong to a different song.**
    -   The lookup started the instant the title changed, while the player was still reporting the *previous*
        song's length - and the length is what picks the right recording. It now waits for the new length.
    -   A search result was accepted if it had timed lyrics, even another artist's song of the same name
        ("Hello" could come back as "Hello, Dolly!"). The title and the artist must now match, or the length must
        agree to within 2 s, and an exact title beats a longer one.
    -   Lyrics cached before this release are looked up again once, and kept if you are offline.

## v1.6.1 - 2026-09-26

### Added

-   **Your own bar logo.** *Settings > Taskbar > Logo > Your own image* takes any PNG, JPG, GIF, BMP or ICO
    (`bar.logo_image`). Square images look best.
-   **Bar colours in Settings.** *Settings > Taskbar > Colours* gives the bar its own background, text and accent
    - type a `#rrggbb` or tap a swatch; "Scheme" hands a colour back to the theme. (The keys existed before, but
    only in the config file, so the bar always wore the main colour scheme.)
-   `-s bar_logo=<name>` switches the bar logo from scripts; `-s bar_hitdump` logs the bar's clickable areas.

### Fixed

-   **An invisible band around the bar swallowed clicks.** Every overlay's window shape was padded (36 px around
    the bar) so shadows would not be clipped, but that padding is also where clicks land, and Windows never passes
    them on to another app - so a window pushed up against the bar could not be clicked along its edge. The
    padding now drops away while the pointer is in that band. Quick settings, notifications, the media toast and
    the dock had the same band.
-   Ten labels in Settings and on the lock screen showed garbled characters (`â€”`) instead of dashes and
    ellipses - double-encoded UTF-8 in the source.

## v1.6.0 - 2026-09-26

A stability release. Every freeze on record was traced to its cause and fixed, everything was then
driven by hand and by stress tests (20 launcher taps, 16 workspace switches, 12 overview toggles and
10 Alt+Tabs back to back, with no stall), and the Guilty Gear Strive motion set was added.

### Added

-   **Guilty Gear Strive motion** (all opt-in, *Settings > Effects > Guilty Gear Strive*)
    -   `strive` motion style: slam past the target, hit-stop, settle; stepped at `motion.strive_fps`
    -   impact frames on panel open: white frame, red frame, slash, decaying shake
    -   workspace banner: slanted call on a black-and-red plate, sliced away on exit
    -   fight intro on startup and/or unlock: letterbox, HUD, a morphing emblem, letters that fly in with a
        red/cyan split and shatter out, shockwaves, camera punch, and a finale that breaks into shards
    -   `-s strive_intro`, `-s strive_banner=<n>` and `_solid` variants to preview them
-   `-s save_test`, `-s blur_dump` self-tests; every slow window message is now named in `stall.txt`
-   Overview refusals are logged with a reason instead of doing nothing silently

### Fixed - freezes

-   **The render loop froze for 8-115 seconds** when the compositor stopped returning from `DwmFlush`.
    Frame pacing now waits at most 50 ms (`PaceFlush`), and the workspace slide and tiling animations use
    the same bounded wait.
-   **The keyboard hook ran on the render thread**, so every render stall delayed every keypress on the
    PC, and Windows could silently remove the hook (Super, Alt+Tab and macros dead until a restart).
    It now has its own thread.
-   The dock read the disk on the render thread twice a second (pinned-app checks, jumbo icons) - up to
    4.9 s stalls on a slow or sleeping drive. Moved to a worker.
-   Sliders rewrote every config file on each frame of a drag. Saves are now spaced 250 ms apart within
    one press, with the last one on release.
-   `Present` could block behind a busy compositor; it no longer waits, and no layer can be starved.
-   A hung tray app could stall the tray-menu reader forever and kill every tray menu until restart.
-   Settings' workspace buttons slept on the render thread.

### Fixed - behaviour

-   Rapid Super taps were dropped, leaving the launcher open after an even number of presses.
-   Super+Tab opened the launcher together with the overview.
-   Held shortcuts auto-repeated and toggled panels open and shut; they now fire once.
-   Right-clicking a tray icon opened the bar's own menu on top of the app's.
-   The tray menu reader never sent `WM_RBUTTONDOWN`, so apps like Voicemeeter never opened their menu; it
    could also read another app's menu, and one failed read was cached for the whole session.
-   The bar menu hung 16 px off the bottom of the screen, and Escape could not close it.
-   Escape could not close the power menu.
-   The clipboard and wallpaper picker needed two Escapes when opened by their own shortcut.
-   Sleep did nothing unless a hibernate had run first (the shutdown privilege was never enabled).
-   `exit(-1)` from single-instance apps (Voicemeeter) was reported as a crash.

### Fixed - visuals

-   The lock screen's backdrop showed **concentric rings instead of a blur**: its aurora glows were eight
    stacked flat discs. They are now one smooth radial gradient (`SoftDisc`).
-   All frost is blurred in 8.8 fixed point and dithered at full size - no more banding.
-   The lock-screen clock was a 64 px glyph stretched to ~300 px; it now uses a 256 px bake.
-   Quick-settings labels ran underneath their buttons.

### Changed

-   `main.cpp` (25,000 lines) is split into `src/app/NN_*.cpp` sections - still one translation unit, and
    proven to produce the same preprocessed program.
-   Shell shortcuts are registered with `MOD_NOREPEAT` (except the wallpaper cyclers).
-   `build.ps1` builds from wherever the repo is cloned.

## v1.5

-   Workspace slide rework on DWM thumbnails, Aether's own tiling animations
-   Taskbar presets, colour schemes and custom bar items
-   The 3-D workspace overview (cube / plane)
-   Media visualiser (WASAPI loopback + FFT)
-   Per-keybind motion graphs with the speed-up rule
