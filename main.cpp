// Aether — a Windows-native rebuild of Caelestia Shell's dashboard.
// (Caelestia's real code is Quickshell/Hyprland/Wayland + /proc + libsensors, which
//  cannot run on Windows; this recreates its UI & behaviour natively in Win32+DX11+
//  ImGui, mapping every Linux stat source to its Windows API equivalent.)
//
// Module 1: the Performance tab (CPU / GPU / Memory / Storage / Network / Battery).
// Build: build.ps1

//
// The shell is split by section into src/app/NN_*.cpp. They are #included here, in order, so it all
// still compiles as ONE translation unit exactly as it did when it was a single file: every static
// global and function is still visible to every later section. To add code, put it in the section it
// belongs to; a new section is a new numbered file plus one line below.

#define AETHER_UNITY
#include "src/app/00_core.cpp"                       // includes, shared types, config plumbing, small utilities
#include "src/app/01_stats.cpp"                      // CPU / GPU / memory / disk / network / battery sampling
#include "src/app/02_weather.cpp"                    // weather (Open-Meteo)
#include "src/app/03_sensors.cpp"                    // temperatures: the sensor sidecar and native AMD GPU temperature
#include "src/app/04_media.cpp"                      // media: WinRT now-playing
#include "src/app/05_drawing.cpp"                    // drawing helpers, fonts, textures, icons
#include "src/app/06_layout.cpp"                     // the layout engine (panel geometry)
#include "src/app/07_journal.cpp"                    // crash-recovery journal, window probes
#include "src/app/08_macros.cpp"                     // the macro engine
#include "src/app/09_settings_registry.cpp"          // the settings registry (SETTINGS table, config load/save)
#include "src/app/10_desktop_clock.cpp"              // the desktop layer: clock, wallpaper, desktop widgets
#include "src/app/11_toasts.cpp"                     // toasts (now playing, recording, notifications)
#include "src/app/12_audio_spectrum.cpp"             // the audio spectrum (WASAPI loopback + FFT) and dashboard cards
#include "src/app/13_shell_tabs.cpp"                 // shell / dashboard tabs
#include "src/app/14_dashboard_tabs.cpp"             // the dashboard tab creator (custom tabs and widgets)
#include "src/app/15_frame_panels.cpp"               // frame-born panels (the Caelestia screen border)
#include "src/app/16_dock.cpp"                       // the dock / running-app list, window rounding, dock icons
#include "src/app/17_shell_mode.cpp"                 // shell mode (Cairo model), komorebi / workspaces
#include "src/app/18_window_previews.cpp"            // window previews (DWM thumbnails)
#include "src/app/19_click_through.cpp"              // click-through regions, workspace slide and tiling glue
#include "src/app/20_switcher.cpp"                   // the window switcher (Alt+Tab)
#include "src/app/21_tray_host.cpp"                  // the system-tray host, audio devices, mixer
#include "src/app/22_bar.cpp"                        // the bar
#include "src/app/23_plugins.cpp"                    // Lua plugins
#include "src/app/24_tray_flyout.cpp"                // the system-tray flyout
#include "src/app/25_bar_preview.cpp"                // in-bar app preview and the bar draw
#include "src/app/26_quick_settings.cpp"             // quick settings
#include "src/app/27_lock.cpp"                       // the lock screen
#include "src/app/28_clipboard.cpp"                  // clipboard history
#include "src/app/29_launcher.cpp"                   // the launcher
#include "src/app/30_settings_app.cpp"               // the Settings app (shell)
#include "src/app/31_settings_pages.cpp"             // Settings pages and their system back-ends
#include "src/app/32_notifications.cpp"              // notifications
#include "src/app/33_snip.cpp"                       // the snipping tool
#include "src/app/34_file_manager.cpp"               // the file manager
#include "src/app/35_decorations.cpp"                // window decorations
#include "src/app/36_boilerplate.cpp"                // window plumbing: render helpers, WndProc, keyboard hook, IPC
#include "src/app/37_winmain.cpp"                    // wWinMain: startup, the render loop, shutdown
