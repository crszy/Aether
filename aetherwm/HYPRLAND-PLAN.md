# Hyprland on Windows: plan

Goal: this komorebi fork becomes Aether's real Hyprland. One Rust program owns the windows, reads
Hyprland's own config, handles the keybinds itself and speaks Hyprland's IPC, so `hyprctl` and every
shell's Hyprland code (Caelestia, DMS, Noctalia, Zenith, end-4) work against it unchanged.

Replaces today's split setup: the C++ tiler inside quickshell-windows, whkd for keys, and the Hyprland
IPC faked inside the shell.

Decided with the user (2026-11-14):
- Config: `~/.config/hypr/hyprland.conf` (Hyprland syntax) **and** `hyprland.lua` (Hyprland's Lua API).
- Keybinds: built into the window manager; whkd is dropped.
- Everything matters: tiling, keybinds/dispatchers, looks/animations, the shells' Hyprland support.
- License: the Komorebi License allows changes and sharing the **source**; Aether builds it on the
  user's PC. Prebuilt binaries only with the licensor's written permission.

## Architecture (inside the fork)

`komorebi/src/hypr/`
- `conf.rs`    hyprland.conf parser: `$vars`, `source =`, nested sections, `a:b = v`, `bind*` flags,
               `windowrule(v2)`, `workspace`, `monitor`, `exec(-once)`, `bezier`, `animation`.
- `lua.rs`     hyprland.lua through mlua (reuse dotfiles/hyprlua/prelude.lua's API mapping).
- `apply.rs`   config -> komorebi state: gaps, borders, rounding, layout, workspaces, rules, animations.
- `binds.rs`   low-level keyboard + mouse hook thread, bind table (mods, submaps, binde repeat,
               bindm drag-move/resize, bindl/bindr), no process per key press.
- `dispatch.rs` Hyprland dispatchers -> komorebi operations (exec, killactive, workspace,
               movetoworkspace(silent), movefocus, movewindow, swapwindow, resizeactive,
               togglefloating, fullscreen, pseudo, togglesplit, togglespecialworkspace,
               focusmonitor, cyclenext, pin, centerwindow, exit, submap, ...).
- `ipc.rs`     Hyprland sockets on named pipes: requests (`j/clients`, `j/workspaces`, `j/monitors`,
               `j/activewindow`, `j/activeworkspace`, `dispatch`, `keyword`, `getoption`, `reload`,
               `binds`, `version`, `[[BATCH]]`) and the event stream (`workspace>>`, `activewindow>>`,
               `activewindowv2>>`, `openwindow>>`, `closewindow>>`, `movewindow>>`, `focusedmon>>`,
               `fullscreen>>`, `submap>>`, `changefloatingmode>>`, `windowtitle>>`, ...).

`komorebi-layouts`: `Dwindle` (Hyprland's split rules: by aspect, preserve_split, force_split,
split_ratio) and `Master` (mfact, orientation, new_status) as real layouts.

quickshell-windows: its Hyprland module and `hyprctl.exe` become clients of the WM's sockets (the
emulation stays only as a fallback when the WM is not running).

## Milestones

1. **Config + keys**: conf parser, apply general/decoration/dwindle basics, built-in binds and the
   core dispatchers; Aether starts the WM instead of whkd.
2. **Tiling**: faithful dwindle + master, resizeactive, togglesplit, pseudo, special workspace,
   fullscreen modes, bindm drag move/resize, floating windows.
3. **IPC**: Hyprland sockets; quickshell-windows + hyprctl switch to them; all shells verified.
4. **Rules + Lua**: windowrulev2, workspace rules, monitor keyword, exec/exec-once, submaps,
   binde repeat, hyprland.lua.
5. **Looks**: `animations {}` (beziers, windows/workspaces/fade styles) onto the fork's animation
   engine, gradient borders, active/inactive opacity, rounding, shadows/blur where DWM allows.
6. **Aether**: Settings switch, installer builds it from source, docs.
7. **Infinite canvas**: a canvas space next to the workspaces (InfiniteDesk's free-window canvas, camera
   pan/zoom). Push the active window onto it and go to it with keys shaped like the workspace ones
   (proposed ALT+C go / ALT+SHIFT+C push, as ALT+N / ALT+SHIFT+N). Plan: InfiniteDesk (our C++,
   `infinitedesk-src/`) gets an "only these windows" mode + a small IPC; the WM hands a window over
   (unmanage, like special workspaces) and takes it back with movetoworkspace N.

## Status

- [x] 1 Config + keys (2026-11-14; all 32 binds of the user's config tested live)
  - [x] hyprland.conf parser (`komorebi/src/hypr/conf.rs`, 7 tests; the user's own config parses with
        no errors: 14 options, 32 binds, rules). `cargo test -p komorebi --lib hypr::`
  - [x] dispatchers -> komorebi (`hypr/dispatch.rs`, every dispatcher in the user's config, tested)
  - [x] built-in binds (`hypr/binds.rs`): keyboard + mouse hooks, submaps, binde/bindr/bindn, bindm drag,
        mask key (no Start menu, no shell launcher). Verified live: SUPER+Return ran `exec wt.exe`.
  - [x] `komorebi --hyprland` applies gaps, borders, layout, runs exec(-once) (`hypr/mod.rs`)
  - [x] Aether: Settings > Hyprland > "Hyprland window manager (beta)" = WinSettings.hyprlandWm ->
        `komorebic start --hyprland`, whkd not started; flipping it restarts komorebi off the UI thread
        (quickshell-windows VirtualDesktops::setKomorebiHyprland). The older C++ Hyprland mode turns off.
  - Known gaps -> milestone 2: special workspace (logs only), togglesplit flips the whole layout,
    resizeactive moves 20 px per side (axis resize), not exact pixels.
- [x] 2 Tiling (2026-11-14)
  - [x] special workspaces (`hypr/special.rs`): movetoworkspace(silent) special:name, togglespecialworkspace,
        back to tiling with movetoworkspace N; komorebi skips them in should_manage. Tested live (SUPER and ALT).
  - [x] ALT as a second main modifier: `--hyprland-alt` (Settings toggle, default on); SUPER+L is Windows' lock.
  - [x] every window tiled: a focused window komorebi holds nowhere is managed (hypr::adopt_unmanaged), and at
        start the open ones it left out (the user's Alacritty was untiled after restarts; ALT+T did nothing on it).
  - [x] scrolling layout on several monitors: scrolled-away columns are parked instead of placed on the
        neighbouring screen (workspace.rs SCROLLED_AWAY), focus moves scroll the strip (komorebi ignores focus
        events of windows it hid), the single-monitor block is off. Tested live with 5 test windows.
  - [x] dwindle: Hyprland's split rule (by the shape of the space, split_width_multiplier, default_split_ratio)
        as layout_options.dwindle on the BSP layout (komorebi-layouts dwindle(), 2 tests); togglesplit flips the
        focused window's split, resizeactive moves one split by exact pixels (hypr::dwindle_action). Tested live:
        left half + right top/bottom, SUPER+J flips only that split, SUPER+CTRL+right = +40 px exactly.
        (preserve_split: splits are recomputed from the shape; flips/ratios are kept per split index.)
  - [x] master layout: layout = master -> komorebi stack layouts by master:orientation (left/right/top/bottom/
        center), master:mfact as the main share; layoutmsg mfact (exact/+-), orientation*/next/prev, swapwithmaster/
        focusmaster (promote). keyword general:layout / master: / dwindle: apply at runtime. Tested: 0.55 -> 0.7, right.
  - [x] fullscreen 0 = the whole monitor (monocle placed over monitor.size, Aether hides its bars), again = back;
        fullscreen 1 = maximize keeping gaps. Tested: frame exactly 0,0 1920x1080.
  - not done (later): master new_status / several masters, pseudotile, exact preserve_split
- [x] 3 IPC (2026-11-14)
  - [x] Hyprland sockets in the WM (`hypr/ipc.rs`): named pipes `$XDG_RUNTIME_DIR/hypr/$SIG/.socket(2).sock`;
        j/clients, workspaces, monitors, activewindow, activeworkspace, dispatch, getoption, binds, version,
        reload, [[BATCH]], ...; events from state diffs (workspace(v2), focusedmon, activewindow(v2), open/close/
        movewindow, windowtitle(v2), changefloatingmode, fullscreen, create/destroyworkspace, submap). Ids: 1-10
        monitor 1, 11-20 monitor 2, ... 20 tests. Verified live over the pipes.
  - [x] Caelestia.exe sets HYPRLAND_INSTANCE_SIGNATURE=aether + XDG_RUNTIME_DIR (komorebi inherits) and makes
        the folder; hyprctl.exe uses the real socket first; Caelestia's HyprExtras reconnects every 2 s - it
        connects once the WM is up (verified).
  - [x] Quickshell.Hyprland dispatch -> the WM when it runs (window dispatchers; ones aimed at another window
        via address:/class: stay in the emulation). Verified: Hyprland.dispatch("workspace 15") from the shell.
  - [x] `keyword` at runtime (one-line config through the same apply path; bind keywords add binds).
        Verified: hyprctl keyword general:gaps_out 30 -> padding 30.
  - [x] the emulation's state from the WM (quickshell-windows hyprland/connection.cpp wmMode): event-socket link
        with reconnect; on each event j/monitors + j/workspaces + j/clients (worker thread) -> workspaces with the
        WM's ids on their real monitors (matched by position, Qt screen names kept), monitors' active workspace,
        each window's workspace by address; the WM's workspace/monitor events forwarded after the state is applied;
        the emulation's own versions + cursor-driven monitor focus off while linked; falls back when the WM goes.
        Verified: shells see 1/13/22 on S27E450/CR240CM/VGA = hyprctl's 1/13/22; a switch reaches them in 0.2-0.7 s.
- [x] 4 Rules + Lua (2026-11-14)
  - [x] window rules (hypr/rules.rs): windowrule/windowrulev2/hl.window_rule, class (exe as written / lower case /
        Win32 class), title, initialClass/Title, negative:; float, tile, workspace N [silent], monitor, size, move,
        center, fullscreen, maximize - on a rules thread when a window opens. keyword windowrulev2 at runtime.
        Tested live: float + size 600x400 + center; workspace 12 silent (focus stayed).
  - [x] workspace rules: gapsin, gapsout, defaultName, on-created-empty (tests)
  - [x] hyprland.lua (hypr/lua.rs): hyprlua.exe (in Aether's qs\) -> HyprConf; hl.dsp.* -> classic dispatchers.
        Hyprland's example config: 48 binds, all but pseudo translate. Preferred over hyprland.conf when present.
  - (submaps, binde repeat, exec/exec-once were done in milestone 1)
- [x] 5 Looks (2026-11-14): hypr/looks.rs - animations {} -> the fork's workspace/window animations (leaf
        fallback windowsMove/windows/global, workspaces slide/slidevert/fade/slidefade(vert), Hyprland speed = 100 ms,
        named + built-in beziers; komorebi.json extras kept), rounding -> Windows 11 corners + border style,
        inactive_opacity -> komorebi transparency; keyword animation/bezier/decoration: at runtime (tested 0.85 ->
        alpha 217). Not done: gradient borders (first colour), active_opacity < 1, blur/shadows (Windows).
- [x] 6 Aether: Settings switch + Alt toggle (M1), installer builds the fork from source when you agree and turns
        Hyprland mode on with a starter hyprland.conf, README section "Hyprland mode".
- [x] 7 Infinite canvas: `movetocanvas` (ALT+SHIFT+C) / `canvas` (ALT+C); InfiniteDesk --aether moves only marked windows, keys only while shown

Overview (ALT+O, the fork's): wallpaper behind it now comes from the shell's own wallpaper window (Explorer's
desktop is hidden under Aether = the black picture), full-screen backdrop like niri, left/right (arrows, H/L,
A/D) + Enter to pick a workspace, Escape / ALT+O to close. Keyboard parts tested live.
