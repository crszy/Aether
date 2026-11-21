

<p align="center">
  <img src="assets/screenshots/bar.png" alt="Zenith Shell Bar">
</p>

<p align="center">
  <sub><sup><img src="https://raw.githubusercontent.com/Tarikul-Islam-Anik/Telegram-Animated-Emojis/main/Activity/Sparkles.webp" alt="Sparkles" width="25" height="25"/></sup></sub>
  <a href="https://github.com/hyprwm/Hyprland">
    <img src="https://img.shields.io/badge/A%20hackable%20shell%20for-Hyprland-0092CD?style=for-the-badge&logo=linux&color=0092CD&logoColor=D9E0EE&labelColor=000000" alt="Shell for Hyprland">
  </a>
  <a href="https://github.com/outfoxxed/quickshell">
    <img src="https://img.shields.io/badge/Powered%20by-Quickshell-FF616D?style=for-the-badge&logo=qt&color=FF616D&logoColor=FFFFFF&labelColor=000000" alt="Powered by Quickshell">
  </a>
  <sub><sup><img src="https://raw.githubusercontent.com/Tarikul-Islam-Anik/Telegram-Animated-Emojis/main/Activity/Sparkles.webp" alt="Sparkles" width="25" height="25"/></sup></sub>
</p>

<div align=center>

![GitHub last commit](https://img.shields.io/github/last-commit/zaeemali272/zenith-shell?style=for-the-badge&labelColor=101418&color=9ccbfb)
![GitHub Repo stars](https://img.shields.io/github/stars/zaeemali272/zenith-shell?style=for-the-badge&labelColor=101418&color=b9c8da)
![GitHub repo size](https://img.shields.io/github/repo-size/zaeemali272/zenith-shell?style=for-the-badge&labelColor=101418&color=d3bfe6)

</div>

---

# 🌌 Zenith Shell

**Zenith Shell** is a modern, modular, high-performance desktop shell custom-built for **Hyprland** using **Quickshell** (Qt6/QML). Designed with sleek glassmorphism aesthetic, anti-aliased dynamic animations, real-time system monitoring, and instant control popups over IPC.

---

<h2><sub><img src="https://raw.githubusercontent.com/Tarikul-Islam-Anik/Animated-Fluent-Emojis/master/Emojis/Objects/Camera%20with%20Flash.png" alt="Camera with Flash" width="25" height="25" /></sub> Screenshots</h2>

<table align="center">
  <tr>
    <td colspan="4"><img src="assets/screenshots/main.png" alt="Main Desktop Overview"></td>
  </tr>
  <tr>
    <td colspan="1"><img src="assets/screenshots/control-center.png" alt="Control Center"></td>
    <td colspan="1"><img src="assets/screenshots/wallpaper.png" alt="Wallpaper Selector"></td>
    <td colspan="1" align="center"><img src="assets/screenshots/power-profile.png" alt="Power Profile"></td>
    <td colspan="1" align="center"><img src="assets/screenshots/bluetooth.png" alt="Bluetooth Manager"></td>
  </tr>
</table>

---

<h2>Technology Stack & Architecture</h2>

Zenith Shell leverages native Wayland protocols and modular QML singletons for near-zero idle resource utilization:

- **UI Framework**: [Quickshell](https://github.com/outfoxxed/quickshell) (Qt6 QML engine with native Wayland Layer Shell, Foreign Toplevel, and Screencopy bindings).
- **Compositor Integration**: Hyprland IPC sockets for real-time workspace state, active window tracking, and monitor scaling.
- **Design System**: Responsive QML design system with dynamic scaling (`Theme.scaled()`), glassmorphic overlays, and customizable Catppuccin-inspired color palettes.
- **Control Surface**: Hyprland global shortcuts (`zenith:<name>`) for keybinds — nothing is forked per keypress — plus a FIFO at `~/.cache/zenith_fifo` and `quickshell ipc` behind `launch.sh` for terminals and scripts.
- **Runtime State**: `~/.local/state/zenith` (launch counts, focus sessions, holidays) and `~/.cache/zenith` (palette, thumbnails, logs); the repo stays clean.
- **Media Engine**: Native Python & `ffmpeg` pipeline for high-performance rounded-corner wallpaper thumbnail generation.

---

<h2><sub><img src="https://raw.githubusercontent.com/Tarikul-Islam-Anik/Animated-Fluent-Emojis/master/Emojis/Objects/Package.png" alt="Package" width="25" height="25" /></sub> System Requirements & Dependencies</h2>

Ensure the following packages are installed on your Linux system:

### Core Requirements
| Component | Package / Dependency | Purpose |
|---|---|---|
| **Shell Engine** | `quickshell` | Qt6/QML shell environment |
| **Compositor** | `hyprland` | Wayland window manager |
| **GUI Framework** | `qt6-declarative`, `qt6-svg`, `qt6-5compat` | QML engine and vector icon rendering |

### System Daemons & Utilities
| Category | Dependencies | Description |
|---|---|---|
| **Wallpapers** | `awww` / `swww`, `mpvpaper`, `ffmpeg` | Static/animated wallpapers & thumbnail generator |
| **Network** | `networkmanager` (`nmcli`) or `iwd` (`iwctl`), `rfkill` | Wi-Fi scanning, connecting & airplane mode |
| **Bluetooth** | `bluez`, `bluez-utils` (`bluetoothctl`) | Device discovery and connection management |
| **Audio & Media** | `pipewire`, `wireplumber`, `playerctl` | System volume control & MPRIS metadata |
| **Power** | `upower`, `power-profiles-daemon` | Battery level tracking & power profile switching |
| **Lock / Idle** | none (Quickshell `WlSessionLock` + `IdleMonitor`, PAM `login` or `zenith-lock`) | Replaces hyprlock + hypridle; Caffeine gates the idle timers (Hyprland ignores layer-shell idle inhibitors) |
| **Helper Runtime** | `python3`, `python-pillow`, `jq` | Async backend scripts & JSON parsing |

---

<h2><sub><img src="https://raw.githubusercontent.com/Tarikul-Islam-Anik/Animated-Fluent-Emojis/master/Emojis/Travel%20and%20places/Rocket.png" alt="Rocket" width="25" height="25" /></sub> Installation & Setup</h2>

### 1. Clone Repository
Clone Zenith Shell into your user configuration directory:
```bash
git clone https://github.com/zaeemali272/zenith-shell.git ~/.config/quickshell
```

### 2. Make Launcher Executable
```bash
chmod +x ~/.config/quickshell/launch.sh
chmod +x ~/.config/quickshell/scripts/*.sh
```

### 3. Autostart in Hyprland
Add the following line to your `hyprland.conf`:
```ini
exec-once = ~/.config/quickshell/launch.sh start
```

---

### ❄️ NixOS & Home Manager (Flakes)

Zenith Shell includes a `flake.nix` with Home Manager module support.

#### 1. Add as a Flake Input
In your NixOS system or Home Manager `flake.nix`:

```nix
{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    zenith-shell.url = "github:zaeemali272/zenith-shell"; # or "git+file:///path/to/local/zenith-shell"
  };

  outputs = { self, nixpkgs, zenith-shell, ... }: {
    homeConfigurations."user" = home-manager.lib.homeManagerConfiguration {
      modules = [
        zenith-shell.homeManagerModules.default
        {
          programs.zenith-shell.enable = true;
        }
      ];
    };
  };
}
```

#### 2. Automatic Updates on `nixos-rebuild`

To automatically fetch and apply updates to Zenith Shell whenever you run `nixos-rebuild`:

- **Updating Remote Flake**: Pass `--update-input` to `nixos-rebuild`:
  ```bash
  sudo nixos-rebuild switch --flake . --update-input zenith-shell
  ```
  *or update lock file explicitly:*
  ```bash
  nix flake update zenith-shell
  sudo nixos-rebuild switch --flake .
  ```

- **Live Local Development**: Point your input to your local git repository:
  ```nix
  zenith-shell.url = "git+file:///home/zaeem/.config/quickshell";
  ```
  Every time you commit changes locally and run `sudo nixos-rebuild switch --flake .`, Nix automatically builds and applies your latest version.


---

<h2><sub><img src="https://raw.githubusercontent.com/Tarikul-Islam-Anik/Animated-Fluent-Emojis/master/Emojis/Symbols/Input%20Latin%20Uppercase.png" alt="Keyboard" width="25" height="25" /></sub> How to Open Menus & IPC Commands</h2>

Every surface is a Hyprland **global shortcut** named `zenith:<action>`, so a keybind reaches the shell through the compositor with no script and no IPC client in between. `launch.sh` is the CLI for terminals and scripts; it takes the same action names.

### IPC Commands

| Action / Menu | Command | Description |
|---|---|---|
| **App Launcher** | `~/.config/quickshell/launch.sh launcher` | Search apps, calculator; Tab cycles to clipboard and emoji |
| **Clipboard / Emoji** | `~/.config/quickshell/launch.sh clipboard` / `emoji` | Clipboard history (cliphist) and emoji picker |
| **Dashboard** | `~/.config/quickshell/launch.sh dashboard` | Overview: notifications, media, calendar, weather |
| **Theme Studio** | `~/.config/quickshell/launch.sh theme` / `launch.sh theme <preset>` | Open Theme Studio or apply curated presets / dynamic wallpaper palette |
| **Wallpaper Selector** | `~/.config/quickshell/launch.sh wallpaper` | Open static & live wallpaper chooser |
| **Power Tools** | `~/.config/quickshell/launch.sh screenshot` / `colorpicker` / `nightlight` / `dnd` | Quick power actions for screenshots, color picker, night light, DND |
| **Wi-Fi / Network** | `~/.config/quickshell/launch.sh wifi` | Open Wi-Fi scan and connection menu |
| **Bluetooth** | `~/.config/quickshell/launch.sh bluetooth` | Open Bluetooth paired & available devices menu |
| **Volume / Audio** | `~/.config/quickshell/launch.sh volume` | Open audio output/input sliders popup |
| **Power Profile** | `~/.config/quickshell/launch.sh powerprofile` | Switch between Performance, Balanced & Power-saver |
| **Battery Details** | `~/.config/quickshell/launch.sh battery` | Open battery health & status popup |
| **Power Menu** | `~/.config/quickshell/launch.sh power` | Open Lock, Logout, Reboot, Shutdown menu |
| **Focus (Pomodoro, Todo, Roadmap)** | `~/.config/quickshell/launch.sh pomodoro` / `roadmap` | Focus timer, task list, roadmap |
| **Mail** | `~/.config/quickshell/launch.sh mail` | Inbox tab |
| **Settings App** | `~/.config/quickshell/launch.sh settings` | Open Zenith Shell configuration window |
| **Lock** | `~/.config/quickshell/launch.sh lock` | Session lock (Quickshell `WlSessionLock` + PAM, caelestia-style card); idle-locks per `Settings/IdleSettings.qml` |
| **Close All Menus** | `~/.config/quickshell/launch.sh close` | Instantly close any open popups or overlays |
| **Shell process** | `~/.config/quickshell/launch.sh start` / `stop` / `restart` / `toggle` | Manage the Quickshell daemon (NixOS-wrapper aware) |

---

### Recommended Hyprland Keybindings

[Hyprland-dots](https://github.com/zaeemali272/Hyprland-dots) already binds everything (see its `variables.lua`), including a Super-tap launcher. On a plain `hyprland.conf` use the `global` dispatcher:

```ini
# Zenith Shell Keybindings -- no script in between
bind = SUPER, A, global, zenith:dashboard
bind = SUPER, T, global, zenith:theme
bind = CTRL SUPER, T, global, zenith:wallpaper
bind = CTRL SUPER, S, global, zenith:volume
bind = SUPER, V, global, zenith:clipboard
bind = SUPER, PERIOD, global, zenith:emoji
bind = CTRL ALT, DELETE, global, zenith:power
bind = CTRL SUPER, I, global, zenith:settings
bindl = SUPER, L, global, zenith:lock
bind = CTRL SUPER, C, global, zenith:close
```

The launcher shortcut (`zenith:launcher`) toggles on the *release* edge so it can be driven by a bare Super tap; bind it with `bindr` if you wire it yourself.

---

<h2><sub><img src="https://raw.githubusercontent.com/Tarikul-Islam-Anik/Animated-Fluent-Emojis/master/Emojis/Objects/Gear.png" alt="Features" width="25" height="25" /></sub> Key Features</h2>

- [x] **Dynamic Top Bar**: Active workspace indicator, window title, system stats, media ticker with live 4-bar reactive audio equalizer, and clock.
- [x] **Theme Studio & Changeable Presets Engine**: 10 curated dotfile themes (Catppuccin Mocha/Macchiato, Tokyo Night, Nord, Gruvbox, Rosé Pine, Cyberpunk, OLED Pitch Black, Dracula, Midnight) + dynamic Material 3 wallpaper palette extraction, custom accent tint overrides, and live zero-restart recoloring.
- [x] **Power-User Quick Action Hub**: Integrated 1-click region screenshot snippet tool, color pipette (`hyprpicker`), Night Light (`gammastep`), Caffeine idle inhibitor, and Do Not Disturb (DND) notification mode.
- [x] **Wallpaper Manager**: Real-time thumbnail preview for images and live video wallpapers (`.mp4`, `.mkv`, `.webm`) using `mpvpaper`.
- [x] **Dynamic Island & Universal Runner**: Smooth animated pill with app search, cliphist clipboard manager, emoji picker, inline math calculator, and terminal command launcher.
- [x] **Control Center**: Integrated quick toggles for Wi-Fi, Bluetooth, Airplane mode, Night Light, Volume, Notifications, Calendar, and Weather.
- [x] **Workspaces Overview**: Visual app launcher grid and workspace switcher integrated with Hyprland IPC.
- [x] **System Stats & Monitoring**: Live CPU, RAM, Disk, Temperature with spring-loaded hover expansion and cat animation power profiles.
- [x] **MPRIS Media Controller**: Album artwork, track info, seekbar, and playback controls.
- [x] **Pomodoro & Todo Widget**: Built-in focus timer with task list management.

---

<h2><sub><img src="https://raw.githubusercontent.com/Tarikul-Islam-Anik/Animated-Fluent-Emojis/master/Emojis/Objects/Test%20Tube.png" alt="Tests" width="25" height="25" /></sub> Checks</h2>

```bash
tests/run.sh          # shell/python syntax, glyphs, helper scripts, todoist sync, mail -- runs anywhere
tests/run.sh --all    # plus a headless smoke test: loads the shell, opens every menu, fails on a QML error
```

`tests/README.md` explains what each check exists to catch. The matching check for the compositor side is `Hyprland-dots/check.sh --live`.

---

<h2><sub><img src="https://raw.githubusercontent.com/Tarikul-Islam-Anik/Telegram-Animated-Emojis/main/Activity/Sparkles.webp" alt="Sparkles" width="25" height="25"/></sub> Inspired By</h2>

- [Axenide/Ax-Shell](https://github.com/Axenide/Ax-Shell)
- [caelestia-dots/shell](https://github.com/caelestia-dots/shell)
- [end-4/dots-hyprland](https://github.com/end-4/dots-hyprland)
