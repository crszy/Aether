# My configs

The configs from the demo, as they are on my machine (personal bits removed).

| File | Goes to |
| --- | --- |
| `komorebi/komorebi.json` | `%USERPROFILE%\komorebi.json` |
| `whkd/whkdrc` | `%USERPROFILE%\.config\whkdrc` |
| `yasb/config.yaml`, `yasb/styles.css`, `yasb/icons/` | `%USERPROFILE%\.config\yasb\` |

Notes:

- `komorebi.json` uses fields which only exist in this fork (`animation.workspace.shadow`,
  `backdrop_profiles`, `border`, `animation.windows`, `close_at_app_start_rules`). The
  `backdrop_profiles` numbers were measured on my machine; drop them if your Terminal / Zed
  look different. The `manage_rules` / `ignore_rules` are for my apps, adjust them to yours.
- yasb is version 2.0.5. In `config.yaml` replace `YOUR_USER` in the home icon path, and set
  `api_key` (a free key from weatherapi.com) and `location` for the weather widget.
- Fonts: JetBrainsMono Nerd Font (`JetBrainsMono NFP`), Segoe Fluent Icons (Windows 11).
- The workspace pill that follows rapid switches needs the yasb patches from
  [`fork-patches/`](../fork-patches); without them it works, it just lags behind.
- whkd bindings call `komorebic`; with the whkd patch from `fork-patches/` they go through one
  `komorebic session` and react faster.
