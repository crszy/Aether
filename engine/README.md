# Quickshell for Windows

[Quickshell](https://quickshell.outfoxxed.me), the QtQuick toolkit for building desktop shells, running natively on
Windows 11 and Windows 10 (version 2004 or later, 22H2 recommended). This repo is the Windows distribution:
the engine with a native Windows backend, QML stand-ins for the Linux-only modules, and a package you can
install with one command and point at your own configs.

It's the same Quickshell that runs [ii-windows](https://github.com/nunreasonable/ii-windows), end-4's
illogical-impulse on Windows, without the shell on top.

This is an unofficial port. It isn't made or supported by the Quickshell developers or by Microsoft, so please
report Windows problems [here](https://github.com/nunreasonable/quickshell-windows/issues), not upstream.

## Install

In PowerShell:

```powershell
irm https://raw.githubusercontent.com/nunreasonable/quickshell-windows/main/windows/install.ps1 | iex
```

It downloads the latest release, checks its SHA-256, installs it in `%LOCALAPPDATA%\Programs\Quickshell` and adds
that folder to your user `PATH`. Nothing is installed system-wide and it doesn't ask for admin rights.

You can also grab the zip from [Releases](https://github.com/nunreasonable/quickshell-windows/releases), extract
it and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1 -Zip .\quickshell-<version>-windows-x64.zip
```

Other options: `-Version v0.3.2-win2` for a specific release, `-Destination <folder>`, `-NoPath`. To remove it:

```powershell
& "$env:LOCALAPPDATA\Programs\Quickshell\install.ps1" -Uninstall
```

## Running a shell

A config is a folder with a `shell.qml`, and on Windows they live in `%LOCALAPPDATA%\quickshell`:

```powershell
qs -c mybar                       # runs %LOCALAPPDATA%\quickshell\mybar\shell.qml
qs -p C:\path\to\shell.qml        # runs a config from anywhere
qs -c mybar ipc call bar toggle   # talks to a running instance (named pipes instead of Unix sockets)
```

`qs.exe` is a console program, handy while writing a config because the log shows up in the terminal.
`qsw.exe` is the same thing without a console window: use it for shortcuts and autostart (a shortcut to
`qsw.exe -c mybar` in `shell:startup`).

There's a small example bar (virtual desktops, the active window, what's playing and a clock):

```powershell
qs -p "$env:LOCALAPPDATA\Programs\Quickshell\examples\bar\shell.qml"
```

## What works

Most of Quickshell's API is backed by Windows itself, so configs written for Linux need few changes.

| Module | On Windows |
|---|---|
| `PanelWindow`, `WlrLayershell` | app bars that reserve screen space, layers above or below windows, input masks, the wallpaper layer |
| `Quickshell.Hyprland` | virtual desktops as workspaces, monitors, toplevels, a subset of `dispatch` |
| `GlobalShortcut` | system-wide hotkeys, including the Windows key on its own |
| `Quickshell.Services.Mpris` | any app that shows up in Windows' media controls |
| `Quickshell.Services.Pipewire` | Core Audio: devices, volume, per-app sessions |
| `Quickshell.Services.Notifications` | a notification server, plus a mirror of Windows' own toasts |
| `Quickshell.Services.SystemTray` | the Windows tray icons |
| `Quickshell.Services.UPower` | battery and power plans |
| `Quickshell.Bluetooth` | adapters, devices, pairing |
| `ScreencopyView` | live window and screen previews through Windows.Graphics.Capture |
| `WindowThumbnail` | window previews drawn by the window manager, which also show minimized windows |
| `Quickshell.Windows` | Windows-only extras: network, brightness, clipboard history, screenshots and OCR, screen recording, wallpaper, and more |

`Quickshell.Services.Pam`, `Polkit` and Wayland's session lock are inert stand-ins: Windows has its own lock
screen and UAC. The modules keep their Linux names so the same config runs on both systems; [WINDOWS.md](WINDOWS.md)
has what each one means on Windows, the full list and how the port works.

## What's here

| | |
|---|---|
| `src/windows/` | the Windows backend: panels, app bars, input masks, hotkeys, virtual desktops, the services above |
| `shims/` | QML stand-ins for modules with no Windows equivalent (`Pam`, `Polkit`, `org.kde.kirigami`...) |
| `windows/` | the toolchain, build, package and install scripts |
| `examples/` | configs to start from |
| [`WINDOWS.md`](WINDOWS.md) | how the port works and what to know when writing configs for Windows |

Everything else is upstream Quickshell.

## Building

The Windows build is cross-compiled from Linux with clang-cl and lld-link, against the MSVC runtime and the
Windows SDK downloaded by [xwin](https://github.com/Jake-Shadle/xwin) and Qt 6.11.2 (`msvc2022_64`) from
[aqtinstall](https://github.com/miurahr/aqtinstall).

```sh
windows/toolchain.sh   # Qt, the CRT and SDK, CLI11 and VirtualDesktopAccessor, in .toolchain/
windows/build.sh       # build/windows/qs.exe and qsw.exe
windows/package.sh     # dist/quickshell-<version>-windows-x64.zip
```

You need clang, lld (or `rust-lld` from rustup), llvm, cmake, ninja, python3, git, curl,
`cargo install xwin --locked` and aqtinstall from git (`pipx install git+https://github.com/miurahr/aqtinstall`;
the 3.3.0 release can't read the Qt 6.11 repository). xwin asks you to accept Microsoft's license the first time.
[WINDOWS.md](WINDOWS.md#building) has the details.

## License

Quickshell is licensed under the GNU LGPL 3 ([LICENSE](LICENSE), [LICENSE-GPL](LICENSE-GPL)), and so is this port.
The package also ships Qt, the Visual C++ runtime and
[VirtualDesktopAccessor](https://github.com/Ciantic/VirtualDesktopAccessor); their licenses are in its `licenses`
folder.

## Credits

- [outfoxxed](https://github.com/outfoxxed) and the contributors of [Quickshell](https://quickshell.outfoxxed.me)
  ([git.outfoxxed.me/quickshell/quickshell](https://git.outfoxxed.me/quickshell/quickshell),
  [mirror](https://github.com/quickshell-mirror/quickshell)), the toolkit this is all built on.
- [minimal-05/quickshell-macos](https://github.com/minimal-05/quickshell-macos), whose macOS port showed this could
  be done.
- [Ciantic/VirtualDesktopAccessor](https://github.com/Ciantic/VirtualDesktopAccessor), for switching virtual
  desktops.

The full history of the Windows port is in [nunreasonable/quickshell](https://github.com/nunreasonable/quickshell/tree/windows).
