# Aether

**Linux rices on Windows, for real.** Aether runs the actual Quickshell desktop shells from the Linux ricing
world — Caelestia, end-4's illogical-impulse, DankMaterialShell, Noctalia, Ryoku, Zenith — natively on Windows,
with AetherWM, a Hyprland-style tiling window manager, underneath.

Aether 2 is a full rewrite. The old native ImGui Aether (v1.x) lives on the
[`native-v1`](https://github.com/crszy/aether/tree/native-v1) branch and the `v1.6.2` tag.

## What it does

- **Real Linux shells on Windows.** Aether's shell is Caelestia ported to Windows, and the bars, docks, launchers,
  lock screens, OSDs, notifications and desktop widgets of end-4, DMS, Noctalia, Ryoku and Zenith are ported next
  to it.
- **Mix & match.** Every part can come from a different shell: a DMS bar, a Noctalia dock, a Caelestia launcher, a
  Ryoku lock screen...
- **AetherWM.** Tiling with Hyprland's config language (`~/.config/hypr/hyprland.conf`), its keybinds, animations,
  workspaces and IPC, so the shells' Hyprland widgets work unchanged.
- **AetherStore.** Paste any Linux dotfiles repo and Aether finds the parts it can use on Windows: Hyprland configs for
  AetherWM, kitty/foot/Alacritty looks as Windows Terminal profiles, fastfetch, Starship, Zen/Firefox themes,
  Hyprlock lock screens, wallpapers, and whole Quickshell shells (Retroism, Nibras, Nucleus, Dux, Ambxst).
  Everything it replaces is backed up, and uninstall puts it back.
- **Your own pieces, no dev build.** Drop a QML file into `~/.config/caelestia/user` and it becomes a bar widget, a
  panel, a lock screen, a volume pop-up, a dashboard tab, a desktop widget, a mod (like Windhawk) or a skin — and it
  reloads live. Aether Settings > *Your pieces* lists them all.
- **Any Quickshell config.** `~/.config/quickshell/<name>` works like `qs -c <name>` on Linux; the shells you run
  are yours to edit.

## In this repo

| Folder | What it is |
| --- | --- |
| `shell/` | Aether's Quickshell config: Caelestia for Windows plus the ported shells (`dms/`, `end4/`, `noctalia/`, `zenith/`), Aether Settings, the store engine (`assets/store.ps1`) |
| `engine/` | The Windows build of Quickshell that runs it all (our fork of quickshell-windows: Windows backend, Hyprland IPC, keybinds, panels, tiling glue) |
| `aetherwm/` | AetherWM, the tiling window manager (Rust) |
| `launcher/` | `Caelestia.exe`: starts the shell, switches configs, syncs your config, watches the shell and restarts it if it dies |
| `cli/` | The `caelestia` command-line tool for Windows |
| `ryoku/` | The Ryoku port (its shell, Go daemons, plugin and picker) |
| `ports/` | The scripts and overrides that port each Linux shell to Windows |
| `store/` | The AetherStore browse list and previews |
| `installer/` | The installer and setup screens |
| `tools/` | Small helpers (m3shapes, hyprlua) |

## Building

Aether is built on Windows with Visual Studio 2022 (C++), Qt 6, Rust (GNU toolchain) and Go. The build scripts in
`ports/`, `installer/` and `engine/` show the steps; they still assume the author's folder layout, so expect to adjust
paths.

## Credits

Aether stands on the work of these projects — go check them out:

- [Quickshell](https://github.com/outfoxxed/quickshell) and [quickshell-windows](https://github.com/nunreasonable/quickshell-windows), the engine
- [Caelestia](https://github.com/caelestia-dots/shell) — Aether's own shell
- [end-4 / dots-hyprland](https://github.com/end-4/dots-hyprland) (illogical-impulse)
- [DankMaterialShell](https://github.com/AvengeMedia/DankMaterialShell)
- [Noctalia](https://github.com/noctalia-dev/noctalia-shell)
- [Ryoku](https://github.com/Ryoku-dev/ryoku)
- [Zenith](https://github.com/zaeemali272/zenith-shell)
- [komorebi](https://github.com/LGUG2Z/komorebi) by LGUG2Z — **AetherWM's blueprint.** AetherWM began as a fork of
  komorebi (via [cipheroute's fork](https://github.com/cipheroute/komorebi)) and grew into its own Hyprland-style window
  manager. komorebi is a great tiling window manager for Windows in its own right; please support it.
- [Hyprland](https://github.com/hyprwm/Hyprland), whose config language, IPC and feel AetherWM follows

See [THIRD-PARTY.md](THIRD-PARTY.md) for licences.

## Licence

GPL-3.0 (see `LICENSE`), except:

- `aetherwm/` is under the **Komorebi License 2.0.0** (`aetherwm/LICENSE.md`), inherited from komorebi: source
  available for personal, non-commercial use.
- `engine/` is LGPL-3.0, like Quickshell.
- Ported code keeps its own licence: DMS and Noctalia MIT, Zenith public domain, `tools/m3shapes` Apache-2.0.
