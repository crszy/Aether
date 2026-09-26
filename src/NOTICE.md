# Aether shell — attribution

Aether is a **native (Win32 + Direct3D 11 + Dear ImGui) reimplementation of
[Caelestia Shell](https://github.com/caelestia-dots/shell)** for Windows.

Caelestia's UI is written in QML for Quickshell (Wayland/Hyprland) and cannot run on
Windows, so each Caelestia source file is ported to its native Aether twin under `src/`,
mirroring the upstream tree — same design tokens, colours, Material-3 motion curves,
layout math and behaviour.

Because Aether is a derivative work of Caelestia, it is licensed under **GPL-3.0**, the
same license as upstream. See the upstream `LICENSE`. Original Caelestia copyright belongs
to the caelestia-dots authors.

## Source-tree mapping (upstream → Aether)
| Caelestia (QML/C++) | Aether (native C++) |
|---|---|
| `components/Anim.qml`, `plugin/.../anim.cpp`, `tokens.hpp` | `src/components/Anim.h` |
| `plugin/.../tokens.hpp` | `src/config/Tokens.h` |
| `modules/bar/Bar.qml` + `components/*` | `src/modules/bar/*` (in progress) |
| `modules/bar/popouts/*` | `src/modules/bar/popouts/*` (in progress) |
| `services/Colours.qml` | `src/services/Colours.h` (in progress) |

Refactor is incremental: `main.cpp` `#include`s each ported module so the build stays a
single, always-green translation unit while the code becomes one-file-per-component.
