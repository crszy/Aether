# komorebi fork: Hyprland-like animations

> **As is.** I made this for myself and publish the source the way it is on my machine.
> It was only ever tested on one PC (Windows 11, two monitors, my apps). There is no
> support and I won't fix things for other setups. You can change it yourself, or hand
> [AGENTS.md](./AGENTS.md) to an AI coding agent and let it build and set it up for you.

Based on komorebi **v0.1.41**. All changes live on the `first-simple` branch.

[![demo](./media/preview.webp)](./media/demo.mp4)

*Click the preview for the full video (48 s, 1080p, 120 fps, AV1).*

## Hyprland mode (Aether)

`komorebi --hyprland` (Aether: Settings > Hyprland > "Hyprland window manager") turns this fork into Hyprland
for Windows. Plan and status: [HYPRLAND-PLAN.md](./HYPRLAND-PLAN.md).

- **Config**: `~/.config/hypr/hyprland.conf` in Hyprland's syntax, or `hyprland.lua` (through Aether's
  `hyprlua.exe`). `$variables`, `source =`, sections, every `bind*` flag, submaps, `exec(-once)`, `env`.
- **Keys**: handled by komorebi itself (no whkd). `--hyprland-alt`: every SUPER bind also works with ALT
  (Windows keeps SUPER+L for locking). `bindm` drags move/resize windows.
- **Dispatchers**: exec, killactive, togglefloating, fullscreen 0/1, movefocus, movewindow, swapwindow,
  resizeactive, workspace / movetoworkspace(silent) (ids 1-10 first monitor, 11-20 second, ...),
  togglespecialworkspace, focusmonitor, togglesplit, layoutmsg, cyclenext, exit, and the extras
  `overview`, `setlayout`, and the infinite canvas: `movetocanvas` pushes the active window onto InfiniteDesk's
  canvas, `canvas` goes there (an empty workspace, InfiniteDesk's pan/zoom keys live) and back; `movetoworkspace N` returns it.
- **Layouts**: dwindle (Hyprland's split rule on the BSP layout; togglesplit flips one split, resizeactive
  moves exact pixels), master (orientation, mfact), scrolling on any number of monitors.
- **Rules**: windowrule / windowrulev2 (float, tile, workspace, monitor, size, move, center, fullscreen,
  maximize), workspace rules (gapsin, gapsout, defaultName, on-created-empty).
- **IPC**: Hyprland's sockets as named pipes at `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/` -
  `hyprctl` (j/clients, workspaces, monitors, activewindow, dispatch, keyword, getoption, binds, reload...)
  and the event socket, so shells written for Hyprland follow it.
- **Looks**: `animations {}` onto this fork's animations (workspaces: slide / slidevert / fade / slidefade,
  windows; Hyprland's beziers), decoration:rounding as Windows 11 corners, inactive_opacity.
- Not on Windows / not done: gradient borders (first colour), blur and shadows (Windows draws them),
  pseudotile, pin, several masters.

## What it does

- **Workspace switches slide** (or fade) on an overlay of live DWM window thumbnails,
  with an optional CSS-style bezier (eg. a slight overshoot).
- **Window moves and resizes animate** the same way: stretched live thumbnails glide from
  the old layout to the new one, the real windows are placed once, underneath.
- **Acrylic/mica windows don't turn see-through** while moving: a blurred wallpaper patch
  is drawn behind each window, with optional per-application calibration.
- **Windows 11 shadows** fade out as an animation starts and back in as it ends.
- **Rounded focus border** drawn around the focused window during animations.
- **Rapid workspace switching**: superseded switches are skipped (the last workspace asked
  for always wins), subscribers such as status bars are told about the new workspace and the
  window about to be focused *before* the switch is carried out, and windows which grab focus
  back a moment after being switched away from don't pull you back to their workspace.
- **`komorebic session`**: reads `komorebic ...` lines from stdin in one long-running process,
  so a hotkey daemon doesn't start a new process (~100 ms each on some machines) per key press.
- **`close_at_app_start_rules`**: windows which open themselves when their application starts
  (eg. the Steam friends list) are closed before they are ever shown; opening them later
  works as usual.
- `komorebic debug-window <hwnd>` (hidden): prints the rule matching of a window.

Measured on my machine: ~1.2 ms of work per animation frame, 140+ fps during layout changes,
a workspace switch announced to the bar 10–35 ms after the key press.

## Configuration

New fields in `komorebi.json` (the published schema doesn't know them, the fork does):

```json
"animation": {
  "enabled": true,
  "duration": 180,
  "fps": 144,
  "style": "EaseOutCubic",
  "workspace": {
    "kind": "Slide",
    "duration": 350,
    "bezier": [0.05, 0.9, 0.1, 1.05],
    "backdrop_blur": true,
    "backdrop_blur_radius": 30,
    "shadow": true,
    "border": { "width": 3, "offset": -1, "radius": 8, "colour": "#FFFFFFFF" },
    "backdrop_profiles": [
      { "exe": "WindowsTerminal.exe", "gain": [1.447, 1.454, 1.49], "offset": [3.9, 3.4, -0.4] },
      { "exe": "Zed.exe", "gain": [0.922, 0.926, 0.939], "offset": [6.3, 6.8, 4.9] }
    ]
  },
  "windows": {
    "duration": 400,
    "bezier": [0.05, 0.9, 0.1, 1.05]
  }
},
"close_at_app_start_rules": [
  [
    { "kind": "Exe", "id": "steamwebhelper.exe", "matching_strategy": "Equals" },
    { "kind": "Class", "id": "SDL_app", "matching_strategy": "Equals" },
    { "kind": "Title", "id": "Friends List", "matching_strategy": "Equals" }
  ]
]
```

| Field | Meaning |
| --- | --- |
| `workspace.kind` | `Slide`, `SlideVertical`, `Fade`, `SlideFade`, `SlideFadeVertical` |
| `workspace.bezier`, `windows.bezier` | `[x1, y1, x2, y2]` like CSS `cubic-bezier`, overrides `style` |
| `workspace.backdrop_blur` | blurred wallpaper behind windows during animations (default `true`) |
| `workspace.backdrop_profiles` | per-app acrylic as `gain * blurred wallpaper + offset` per RGB channel. **The numbers above were measured on my machine with my themes**; without a profile an acrylic app may brighten/darken slightly when the overlay appears and goes |
| `workspace.shadow` | fade Windows 11 shadows instead of popping them (default `true`) |
| `workspace.border` | focus border during animations (default: none) |
| `windows` | window move/resize animations on the overlay (on when the section is present) |
| `close_at_app_start_rules` | windows closed when they appear within 40 s of their process starting |

## My configs

The komorebi, whkd and yasb configs from the demo are in [`dotfiles/`](./dotfiles).

## Optional extras (patches, not part of this repository's build)

In [`fork-patches/`](./fork-patches):

- `whkd-0.2.10-komorebic-session.patch` — whkd sends `komorebic ...` bindings to one
  `komorebic session` instead of starting a process per key press. Apply to
  [whkd](https://github.com/LGUG2Z/whkd) v0.2.10 and build it.
- `yasb-2.0.5-fast-workspace-switching.patch` — [yasb](https://github.com/amnweb/yasb)
  v2.0.5: the komorebi workspaces widget follows rapid switches, the active window widget
  shows the window komorebi is about to focus, the komorebi pipe is polled every 5 ms.
- `qt-css-engine-2a178cf-no-full-size-frame.patch` — the CSS transition engine yasb uses:
  no one-frame flash of the full size when a transition restarts.

I applied the yasb ones by recompiling the changed modules into yasb's `lib/library.zip`,
which is a hack; building yasb from source with the patches is the clean way.

## License

Same as upstream: [Komorebi License 2.0.0](./LICENSE.md). It allows distributing the
**source code** with changes for personal, non-commercial use only, so there are no binaries
here: build it yourself.

## Upstream

This is a fork of [komorebi](https://github.com/LGUG2Z/komorebi) by LGUG2Z. Its original
README is kept in [README-upstream.md](./README-upstream.md); for everything not described
above (installation, configuration, keybindings) see the
[komorebi documentation](https://lgug2z.github.io/komorebi/).
