//! Hyprland on Windows: Hyprland's config, keybinds, dispatchers and IPC on top of komorebi.
//! Plan and status: HYPRLAND-PLAN.md at the repository root.

pub mod conf;
pub mod dispatch;
pub mod binds;
pub mod special;
pub mod ipc;
pub mod lua;
pub mod rules;
pub mod looks;

use std::path::PathBuf;

/// `~/.config/hypr/hyprland.lua` (Hyprland reads it instead of hyprland.conf when it exists)
pub fn lua_path() -> PathBuf {
    config_path().with_extension("lua")
}

/// the config in use: hyprland.lua when it exists (through hyprlua.exe), else hyprland.conf
pub fn load_config() -> conf::HyprConf {
    // Aether: niri mode reads ~/.config/niri/config.kdl, translated into the same config
    if crate::niri::mode() {
        return crate::niri::load_as_hypr();
    }
    // Hyprland's scrolling layout: a lone column fills the screen (fullscreen_on_one_column, on by default)
    crate::niri::set_config(crate::niri::NiriConfig::default());
    let lua = lua_path();
    let mut conf = if std::env::var_os("HYPRLAND_CONFIG").is_none() && !crate::niri::infinitedesk() && lua.is_file() {
        lua::load(&lua)
    } else {
        conf::HyprConf::load(&config_path())
    };
    // Ryoku (Aether's Ryoku desktop): while it runs, its Hub's Hyprland settings (gaps, borders, blur, animations -
    // settings.lua next to hyprland.conf) apply on top. Its rebinds and monitor files are left out: they target
    // Ryoku's Linux scripts and monitor names.
    let ryoku = config_path().with_file_name("settings.lua");
    if ryoku_running() && ryoku.is_file() {
        lua::merge(&ryoku, &mut conf);
    }
    conf
}

/// Ryoku's daemon is up: its control pipe (\\.\pipe\<%LOCALAPPDATA%/AetherShell/ryoku-run>/ryoku-shell.sock) exists
pub fn ryoku_running() -> bool {
    let Some(local) = std::env::var_os("LOCALAPPDATA") else { return false };
    let run = PathBuf::from(local).join("AetherShell").join("ryoku-run").join("ryoku-shell.sock");
    let pipe = format!(r"\\.\pipe\{}", run.to_string_lossy().replace('\\', "/"));
    // GetFileAttributesW asks without opening the pipe (opening it would connect a client to the daemon)
    let wide: Vec<u16> = pipe.encode_utf16().chain(std::iter::once(0)).collect();
    let attrs = unsafe { windows::Win32::Storage::FileSystem::GetFileAttributesW(windows::core::PCWSTR(wide.as_ptr())) };
    attrs != windows::Win32::Storage::FileSystem::INVALID_FILE_ATTRIBUTES
}

/// `%USERPROFILE%\.config\hypr\hyprland.conf` (or `$HYPRLAND_CONFIG`, like Hyprland's `--config`)
pub fn config_path() -> PathBuf {
    if crate::niri::mode() {
        return crate::niri::config_path();
    }
    // InfiniteDesk mode: its own config next to hyprland.conf (hyprland.conf while it has none)
    if crate::niri::infinitedesk() && std::env::var_os("HYPRLAND_CONFIG").is_none() {
        let home = std::env::var_os("USERPROFILE").or_else(|| std::env::var_os("HOME")).map(PathBuf::from).unwrap_or_default();
        let desk = home.join(".config").join("hypr").join("infinitedesk.conf");
        if desk.is_file() {
            return desk;
        }
    }
    if let Some(path) = std::env::var_os("HYPRLAND_CONFIG") {
        return PathBuf::from(path);
    }
    let home = std::env::var_os("USERPROFILE").or_else(|| std::env::var_os("HOME")).map(PathBuf::from).unwrap_or_default();
    home.join(".config").join("hypr").join("hyprland.conf")
}

use std::sync::Arc;

use parking_lot::Mutex;

use crate::core::DefaultLayout;
use crate::core::SocketMessage;
use crate::core::WindowKind;
use crate::window_manager::WindowManager;

/// `rgba(33ccffee)`, `rgb(33ccff)`, `0xee33ccff` -> (r, g, b); the first colour of a gradient
pub fn parse_colour(value: &str) -> Option<(u32, u32, u32)> {
    let first = value.split_whitespace().next()?.trim();
    let hex = if let Some(inner) = first.strip_prefix("rgba(").or_else(|| first.strip_prefix("rgb(")) {
        inner.trim_end_matches(')').to_string()
    } else if let Some(argb) = first.strip_prefix("0x") {
        // 0xAARRGGBB
        argb.get(2..).unwrap_or(argb).to_string()
    } else {
        return None;
    };
    let rgb = u32::from_str_radix(hex.get(0..6)?, 16).ok()?;
    Some(((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff))
}

/// Hyprland's layouts on komorebi's (until the real dwindle / master of milestone 2)
pub fn layout_for(name: &str) -> Option<DefaultLayout> {
    match name.trim() {
        "dwindle" => Some(DefaultLayout::BSP),
        "master" => None, // apply_master (orientation + mfact)
        "scrolling" => Some(DefaultLayout::Scrolling),
        _ => None, // "canvas" and others: leave the workspace as it is
    }
}

/// The config as komorebi operations (gaps, borders, layout on every workspace).
pub fn apply_messages(conf: &conf::HyprConf, workspaces: &[(usize, usize)]) -> Vec<SocketMessage> {
    let mut out = Vec::new();
    let gaps_in = conf.get_i64("general:gaps_in").map(|v| v as i32);
    // gaps_out may be "12" or "12 12 12 12" (css order): the first value for now
    let gaps_out = conf.get("general:gaps_out").and_then(|v| v.split_whitespace().next()?.parse::<i32>().ok());
    let layout = conf.get("general:layout").and_then(layout_for);
    for &(monitor, workspace) in workspaces {
        if let Some(gap) = gaps_in {
            // Hyprland's gaps_in is per side between windows; komorebi's container padding is the same idea
            out.push(SocketMessage::ContainerPadding(monitor, workspace, gap));
        }
        if let Some(gap) = gaps_out {
            out.push(SocketMessage::WorkspacePadding(monitor, workspace, gap));
        }
        if let Some(layout) = layout {
            out.push(SocketMessage::WorkspaceLayout(monitor, workspace, layout));
        }
    }
    if let Some(width) = conf.get_i64("general:border_size") {
        out.push(SocketMessage::Border(width > 0));
        out.push(SocketMessage::BorderWidth(width as i32));
    }
    if let Some((r, g, b)) = conf.get("general:col.active_border").and_then(parse_colour) {
        // every focused kind gets it (a stack, monocle or floating window would keep komorebi's blue otherwise)
        for kind in [WindowKind::Single, WindowKind::Stack, WindowKind::Monocle, WindowKind::Floating] {
            out.push(SocketMessage::BorderColour(kind, r, g, b));
        }
    }
    if let Some((r, g, b)) = conf.get("general:col.inactive_border").and_then(parse_colour) {
        out.push(SocketMessage::BorderColour(WindowKind::Unfocused, r, g, b));
    }
    out
}

static ALT_TOO: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

/// re-reads hyprland.conf and applies it again (hyprctl reload): binds, gaps, borders, layout - no exec-once
pub fn reload(wm: &Arc<Mutex<WindowManager>>) {
    *looks::ANIM_OVERRIDE.lock().unwrap() = None; // the file decides again
    let conf = load_config();
    load(wm, &conf);
    tracing::info!("hypr: reloaded ({} binds)", conf.binds.len());
}

fn load(wm: &Arc<Mutex<WindowManager>>, conf: &conf::HyprConf) {
    let alt_too = ALT_TOO.load(std::sync::atomic::Ordering::SeqCst);
    let all = if alt_too { binds::with_alt_variants(&conf.binds) } else { conf.binds.clone() };
    let unknown = binds::set_binds(&all);
    if !unknown.is_empty() {
        tracing::warn!("hypr: keys not understood: {unknown:?}");
    }
    let workspaces: Vec<(usize, usize)> = {
        let wm = wm.lock();
        wm.monitors()
            .iter()
            .enumerate()
            .flat_map(|(m, monitor)| (0..monitor.workspaces().len()).map(move |w| (m, w)))
            .collect()
    };
    for message in apply_messages(conf, &workspaces) {
        if let Err(error) = crate::process_command::run_command(wm, message) {
            tracing::warn!("hypr: applying the config: {error}");
        }
    }
    apply_dwindle(wm, conf);
    apply_master(wm, conf);
    let count = rules::set_rules(&conf.window_rules, rules::parse_workspace_rules(conf));
    if count > 0 {
        tracing::info!("hypr: {count} window rules");
    }
    for message in rules::workspace_rule_messages() {
        let _ = crate::process_command::run_command(wm, message);
    }
    looks::apply(wm, conf);
}

/// `komorebi --hyprland`: reads hyprland.conf, applies it, starts the keybinds, runs `exec-once`.
/// `alt_too` (`--hyprland-alt`): every SUPER bind also works with ALT.
pub fn start(wm: Arc<Mutex<WindowManager>>, alt_too: bool) {
    let path = if lua_path().is_file() && std::env::var_os("HYPRLAND_CONFIG").is_none() { lua_path() } else { config_path() };
    let conf = load_config();
    for error in &conf.errors {
        tracing::warn!("hyprland.conf: {error}");
    }
    tracing::info!("hypr: {} binds, {} options from {}", conf.binds.len(), conf.options.len(), path.display());

    ALT_TOO.store(alt_too, std::sync::atomic::Ordering::SeqCst);
    special::canvas_adopt_marked(); // canvas windows from before a restart
    if alt_too {
        let extra = binds::with_alt_variants(&conf.binds).len() - conf.binds.len();
        tracing::info!("hypr: ALT works like SUPER ({extra} binds with both)");
    }
    load(&wm, &conf);


    for command in &conf.exec_once {
        binds::exec(command);
    }
    for command in &conf.exec {
        binds::exec(command);
    }

    ACTIVE.store(true, std::sync::atomic::Ordering::SeqCst);
    rules::start(wm.clone());
    adopt_open_windows(&wm);
    ipc::start(wm.clone());
    binds::start(wm);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn colours() {
        assert_eq!(parse_colour("rgba(33ccffee) rgba(00ff99ee) 45deg"), Some((0x33, 0xcc, 0xff)));
        assert_eq!(parse_colour("rgb(595959)"), Some((0x59, 0x59, 0x59)));
        assert_eq!(parse_colour("0xee33ccff"), Some((0x33, 0xcc, 0xff)));
        assert_eq!(parse_colour("nonsense"), None);
    }

    #[test]
    fn the_users_config_applies() {
        let c = conf::HyprConf::parse_str("general {\n gaps_in = 5\n gaps_out = 12\n border_size = 2\n layout = dwindle\n col.active_border = rgba(33ccffee) rgba(00ff99ee) 45deg\n}\n", std::path::Path::new("."));
        let messages = apply_messages(&c, &[(0, 0), (0, 1)]);
        let text = serde_json::to_string(&messages).unwrap();
        assert!(text.contains("ContainerPadding"), "{text}");
        assert!(text.contains("WorkspacePadding"));
        assert!(text.contains("BSP"));
        assert!(text.contains("BorderWidth"));
        assert_eq!(messages.len(), 3 * 2 + 3);
    }
}

// ------------------------------------------------------------------ every window is tiled (like Hyprland)

/// set by `start`: komorebi runs as the Hyprland window manager
pub static ACTIVE: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

fn managed_anywhere(wm: &WindowManager, hwnd: isize) -> bool {
    wm.monitors().iter().any(|m| m.workspaces().iter().any(|w| w.contains_window(hwnd)))
}

/// A window komorebi does not hold anywhere (it was already open when komorebi started, or came back from somewhere
/// komorebi lost track of it) is handed to komorebi, like Hyprland manages every window. Returns true when it
/// queued that (the Manage event does the rest).
pub fn adopt_unmanaged(wm: &WindowManager, window: crate::Window) -> bool {
    if !ACTIVE.load(std::sync::atomic::Ordering::SeqCst) {
        return false;
    }
    if managed_anywhere(wm, window.hwnd) || special::is_special(window.hwnd) || crate::windows_api::WindowsApi::is_iconic(window.hwnd) {
        return false;
    }
    let mut debug = crate::window::RuleDebug::default();
    if !matches!(window.should_manage(None, &mut debug), Ok(true)) {
        return false;
    }
    tracing::info!("hypr: managing {} ({}) - it was not tiled", window.hwnd, window.exe().unwrap_or_default());
    let _ = crate::winevent_listener::event_tx().send(crate::window_manager_event::WindowManagerEvent::Manage(window));
    true
}

/// at start: every open window that komorebi left out
fn adopt_open_windows(wm: &Arc<Mutex<WindowManager>>) {
    let Ok(windows) = crate::windows_api::WindowsApi::alt_tab_windows() else { return };
    let wm = wm.lock();
    let adopted = windows.into_iter().filter(|w| adopt_unmanaged(&wm, *w)).count();
    if adopted > 0 {
        tracing::info!("hypr: {adopted} open windows were not tiled - tiling them now");
    }
}

// ------------------------------------------------------------------ dwindle (Hyprland's layout on komorebi's BSP)

fn empty_layout_options() -> crate::core::LayoutOptions {
    crate::core::LayoutOptions { scrolling: None, grid: None, column_ratios: None, row_ratios: None, dwindle: None }
}

/// turns Hyprland's dwindle on for every workspace (keeps flips / ratios already made)
fn apply_dwindle(wm: &Arc<Mutex<WindowManager>>, conf: &conf::HyprConf) {
    let split = conf.get_f64("dwindle:force_split").unwrap_or(0.0).clamp(0.0, 2.0) as u8;
    crate::animation::FORCE_SPLIT.store(split, std::sync::atomic::Ordering::Relaxed);
    let live = conf.get_bool("aether:toggle_live_resize").unwrap_or(true);
    crate::animation::TOGGLE_LIVE_RESIZE.store(live, std::sync::atomic::Ordering::Relaxed);
    if conf.get_bool("aether:desktop_window").unwrap_or(true) { crate::progman::ensure(); }
    let layout = conf.get("general:layout").unwrap_or("dwindle").trim().to_string();
    // dwindle is Hyprland's default; canvas is not part of the window manager yet: dwindle meanwhile
    if !matches!(layout.as_str(), "dwindle" | "canvas" | "") {
        return;
    }
    let multiplier = conf.get_f64("dwindle:split_width_multiplier").map(|v| v as f32);
    // Hyprland's default_split_ratio is 0.1-1.9 with 1.0 = even; ours is the share given to the first window
    let default_ratio = conf.get_f64("dwindle:default_split_ratio").map(|v| (v as f32 / 2.0).clamp(0.05, 0.95));
    let mut wm = wm.lock();
    for monitor in wm.monitors_mut() {
        for workspace in monitor.workspaces_mut() {
            let mut options = workspace.layout_options.unwrap_or_else(empty_layout_options);
            let mut dwindle = options.dwindle.unwrap_or_default();
            dwindle.split_width_multiplier = multiplier;
            dwindle.default_ratio = default_ratio;
            options.dwindle = Some(dwindle);
            workspace.layout_options = Some(options);
        }
    }
    let _ = wm.retile_all(false);
}

/// `togglesplit` / `resizeactive` on a dwindle workspace, done on the split itself. False when the focused workspace
/// is not dwindle (the generic dispatchers then run).
pub fn dwindle_action(wm: &Arc<Mutex<WindowManager>>, dispatcher: &str, arg: &str) -> bool {
    let mut wm = wm.lock();
    let Ok(workspace) = wm.focused_workspace_mut() else { return false };
    if !matches!(workspace.layout, crate::core::Layout::Default(DefaultLayout::BSP)) {
        return false;
    }
    let Some(mut options) = workspace.layout_options.and_then(|o| o.dwindle) else { return false };
    let len = workspace.containers().len();
    let focused = workspace.focused_container_idx();
    if len < 2 {
        return true;
    }
    match dispatcher {
        "togglesplit" => {
            // the split that made the focused window's place (the last window shares the one before)
            let split = if focused + 1 >= len { len - 2 } else { focused };
            if split < 32 {
                options.flips ^= 1 << split;
            }
        }
        "resizeactive" => {
            let mut parts = arg.split_whitespace();
            let dx: f32 = parts.next().and_then(|v| v.trim_end_matches("px").parse().ok()).unwrap_or(0.0);
            let dy: f32 = parts.next().and_then(|v| v.trim_end_matches("px").parse().ok()).unwrap_or(0.0);
            // the space the layout fills: the bounding box of the current tiles
            let tiles = &workspace.latest_layout;
            if tiles.len() != len {
                return false;
            }
            let left = tiles.iter().map(|r| r.left).min().unwrap_or(0);
            let top = tiles.iter().map(|r| r.top).min().unwrap_or(0);
            let right = tiles.iter().map(|r| r.left + r.right).max().unwrap_or(0);
            let bottom = tiles.iter().map(|r| r.top + r.bottom).max().unwrap_or(0);
            let area = crate::core::Rect { left, top, right: right - left, bottom: bottom - top };
            let (_, splits) = komorebi_layouts::dwindle(&area, len, &options);
            for (delta, side_by_side) in [(dx, true), (dy, false)] {
                if delta == 0.0 {
                    continue;
                }
                // the focused window grows: its own split (it is the first part) or the nearest split above it (it is
                // in the rest) that runs this way
                let pick = if focused < splits.len() && splits[focused].side_by_side == side_by_side {
                    Some((focused, 1.0))
                } else {
                    (0..focused.min(splits.len())).rev().find(|j| splits[*j].side_by_side == side_by_side).map(|j| (j, -1.0))
                };
                let Some((split, sign)) = pick else { continue };
                if split >= komorebi_layouts::DWINDLE_MAX_SPLITS {
                    continue;
                }
                let size = if side_by_side { splits[split].area.right } else { splits[split].area.bottom } as f32;
                if size <= 0.0 {
                    continue;
                }
                let current = Some(options.ratios[split]).filter(|r| *r > 0.0).unwrap_or(options.default_ratio.unwrap_or(0.5));
                options.ratios[split] = (current + sign * delta / size).clamp(0.05, 0.95);
            }
        }
        _ => return false,
    }
    let mut layout_options = workspace.layout_options.unwrap_or_else(empty_layout_options);
    layout_options.dwindle = Some(options);
    workspace.layout_options = Some(layout_options);
    let follow = wm.mouse_follows_focus;
    let _ = wm.update_focused_workspace(follow, false);
    true
}

// ------------------------------------------------------------------ master layout

fn master_layout(orientation: &str) -> DefaultLayout {
    match orientation.trim() {
        "right" => DefaultLayout::RightMainVerticalStack,
        "top" | "bottom" => DefaultLayout::HorizontalStack,
        "center" => DefaultLayout::UltrawideVerticalStack,
        _ => DefaultLayout::VerticalStack,
    }
}

fn with_main_ratio(options: Option<crate::core::LayoutOptions>, mfact: f32) -> crate::core::LayoutOptions {
    let mut options = options.unwrap_or_else(empty_layout_options);
    let mut ratios = options.column_ratios.unwrap_or([None; komorebi_layouts::MAX_RATIOS]);
    ratios[0] = Some(mfact.clamp(0.1, 0.9));
    options.column_ratios = Some(ratios);
    options
}

/// `layout = master`: komorebi's stack layouts by master:orientation, master:mfact as the main area's share
fn apply_master(wm: &Arc<Mutex<WindowManager>>, conf: &conf::HyprConf) {
    if conf.get("general:layout").map(str::trim) != Some("master") {
        return;
    }
    let layout = master_layout(conf.get("master:orientation").unwrap_or("left"));
    let mfact = conf.get_f64("master:mfact").unwrap_or(0.55) as f32;
    let mut wm = wm.lock();
    for monitor in wm.monitors_mut() {
        for workspace in monitor.workspaces_mut() {
            workspace.layout = crate::core::Layout::Default(layout);
            workspace.layout_options = Some(with_main_ratio(workspace.layout_options, mfact));
        }
    }
    let _ = wm.retile_all(false);
}

/// `layoutmsg` on a master workspace: mfact exact N / mfact +-N, orientationleft/right/top/bottom/center/next/prev
pub fn master_action(wm: &Arc<Mutex<WindowManager>>, arg: &str) -> bool {
    let mut words = arg.split_whitespace();
    let command = words.next().unwrap_or("");
    let mut wm = wm.lock();
    let Ok(workspace) = wm.focused_workspace_mut() else { return false };
    let current = match workspace.layout {
        crate::core::Layout::Default(
            l @ (DefaultLayout::VerticalStack
            | DefaultLayout::RightMainVerticalStack
            | DefaultLayout::HorizontalStack
            | DefaultLayout::UltrawideVerticalStack),
        ) => l,
        _ => return false,
    };
    match command {
        "mfact" => {
            let (exact, value) = match (words.next(), words.next()) {
                (Some("exact"), Some(v)) => (true, v),
                (Some(v), _) => (false, v),
                _ => return true,
            };
            let Ok(value) = value.parse::<f32>() else { return true };
            let now = workspace.layout_options.and_then(|o| o.column_ratios).and_then(|r| r[0]).unwrap_or(0.5);
            let mfact = if exact { value } else { now + value };
            workspace.layout_options = Some(with_main_ratio(workspace.layout_options, mfact));
        }
        c if c.starts_with("orientation") => {
            let next = match &c["orientation".len()..] {
                "next" => match current {
                    DefaultLayout::VerticalStack => DefaultLayout::HorizontalStack,
                    DefaultLayout::HorizontalStack => DefaultLayout::RightMainVerticalStack,
                    DefaultLayout::RightMainVerticalStack => DefaultLayout::UltrawideVerticalStack,
                    _ => DefaultLayout::VerticalStack,
                },
                "prev" => match current {
                    DefaultLayout::VerticalStack => DefaultLayout::UltrawideVerticalStack,
                    DefaultLayout::UltrawideVerticalStack => DefaultLayout::RightMainVerticalStack,
                    DefaultLayout::RightMainVerticalStack => DefaultLayout::HorizontalStack,
                    _ => DefaultLayout::VerticalStack,
                },
                side => master_layout(side),
            };
            workspace.layout = crate::core::Layout::Default(next);
        }
        _ => return false,
    }
    let follow = wm.mouse_follows_focus;
    let _ = wm.update_focused_workspace(follow, false);
    true
}

// ------------------------------------------------------------------ fullscreen 0: the whole monitor

static FULLSCREEN: std::sync::LazyLock<Mutex<std::collections::HashMap<isize, crate::core::Rect>>> =
    std::sync::LazyLock::new(|| Mutex::new(std::collections::HashMap::new()));

/// the rect of a window in true fullscreen (workspace.rs places the monocle window there, not in the work area)
pub fn fullscreen_rect(hwnd: isize) -> Option<crate::core::Rect> {
    FULLSCREEN.lock().get(&hwnd).copied()
}

/// `fullscreen 0`: the focused window covers its whole monitor - Aether hides its bars for an app that covers the
/// monitor. Again: back to tiling.
pub fn toggle_true_fullscreen(wm: &Arc<Mutex<WindowManager>>) {
    let Ok(hwnd) = crate::windows_api::WindowsApi::foreground_window() else { return };
    let (size, in_monocle) = {
        let wm = wm.lock();
        let Some(monitor) = wm.focused_monitor() else { return };
        let in_monocle = wm.focused_workspace().map(|w| w.monocle_container.is_some()).unwrap_or(false);
        (monitor.size, in_monocle)
    };
    let leaving = FULLSCREEN.lock().remove(&hwnd).is_some();
    if leaving {
        if in_monocle {
            let _ = crate::process_command::run_command(wm, crate::core::SocketMessage::ToggleMonocle);
        }
        return;
    }
    FULLSCREEN.lock().insert(hwnd, size);
    let message = if in_monocle { crate::core::SocketMessage::Retile } else { crate::core::SocketMessage::ToggleMonocle };
    let _ = crate::process_command::run_command(wm, message);
}

/// dwindle / master options for a (partial) config: hyprland.conf on load, or a `keyword general:layout ...`
pub fn apply_layouts(wm: &Arc<Mutex<WindowManager>>, conf: &conf::HyprConf) {
    apply_dwindle(wm, conf);
    apply_master(wm, conf);
    let count = rules::set_rules(&conf.window_rules, rules::parse_workspace_rules(conf));
    if count > 0 {
        tracing::info!("hypr: {count} window rules");
    }
    for message in rules::workspace_rule_messages() {
        let _ = crate::process_command::run_command(wm, message);
    }
}

/// Aether: windows the WM never tiles. A game in borderless fullscreen (no caption, covering its whole monitor) is
/// left exactly where it is, so tiling never moves, resizes or redraws around it; no-activate windows are popups
/// (notification toasts); Steam's "who is online / new message / now playing" toasts are steamwebhelper windows
/// titled notificationtoasts_*.
/// Aether: a splash screen (an app's "starting / loading / updating" window) is never tiled - it stays where the
/// app put it until it closes. Checked before every manage rule, so not even a forced "manage" can tile one.
/// A splash is:
///   - named so: title or class containing splash / updater / "loading" / "starting", or a known splash class; or
///   - built like one: no title-bar frame, not resizable, no maximise button, and smaller than ~3/4 of its monitor
///     (real apps without a title bar - Discord, Spotify, VS Code - still keep their maximise button)
pub fn is_splash(hwnd: isize) -> bool {
    use windows::Win32::Foundation::HWND;
    use windows::Win32::Graphics::Gdi::GetMonitorInfoW;
    use windows::Win32::Graphics::Gdi::MONITOR_DEFAULTTONEAREST;
    use windows::Win32::Graphics::Gdi::MONITORINFO;
    use windows::Win32::Graphics::Gdi::MonitorFromWindow;
    use windows::Win32::UI::WindowsAndMessaging::GWL_STYLE;
    use windows::Win32::UI::WindowsAndMessaging::GetWindowLongPtrW;
    use windows::Win32::UI::WindowsAndMessaging::GetWindowRect;

    const WS_CAPTION: isize = 0x00C0_0000;
    const WS_THICKFRAME: isize = 0x0004_0000;
    const WS_MAXIMIZEBOX: isize = 0x0001_0000;

    let title = crate::windows_api::WindowsApi::window_text_w(hwnd).unwrap_or_default().to_lowercase();
    let class = crate::windows_api::WindowsApi::real_window_class_w(hwnd).unwrap_or_default().to_lowercase();

    // named like a splash
    const CLASSES: [&str; 6] = ["splash", "bootstrapupdateuiclass", "msosplash", "vssplashscreen", "sunawtwindow", "jetbrains-splash"];
    if CLASSES.iter().any(|c| class.contains(c)) {
        return true;
    }
    const TITLES: [&str; 6] = ["splash", "updater", "updating", "loading...", "starting...", "is starting"];
    if TITLES.iter().any(|t| title.contains(t)) {
        return true;
    }

    // built like a splash
    unsafe {
        let h = HWND(hwnd as _);
        let style = GetWindowLongPtrW(h, GWL_STYLE) as isize;
        if style & WS_CAPTION == WS_CAPTION || style & WS_THICKFRAME != 0 || style & WS_MAXIMIZEBOX != 0 {
            return false;
        }
        let mut rect = Default::default();
        if GetWindowRect(h, &mut rect).is_err() {
            return false;
        }
        let mut info = MONITORINFO { cbSize: std::mem::size_of::<MONITORINFO>() as u32, ..Default::default() };
        if !GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mut info).as_bool() {
            return false;
        }
        let m = info.rcWork;
        let (w, hgt) = ((rect.right - rect.left) as i64, (rect.bottom - rect.top) as i64);
        let (mw, mh) = ((m.right - m.left) as i64, (m.bottom - m.top) as i64);
        w > 0 && hgt > 0 && w * 4 < mw * 3 && hgt * 4 < mh * 3
    }
}

pub fn leave_alone(hwnd: isize) -> bool {
    use windows::Win32::Foundation::HWND;
    use windows::Win32::Graphics::Gdi::GetMonitorInfoW;
    use windows::Win32::Graphics::Gdi::MONITOR_DEFAULTTONEAREST;
    use windows::Win32::Graphics::Gdi::MONITORINFO;
    use windows::Win32::Graphics::Gdi::MonitorFromWindow;
    use windows::Win32::UI::WindowsAndMessaging::GWL_STYLE;
    use windows::Win32::UI::WindowsAndMessaging::GetWindowLongPtrW;
    use windows::Win32::UI::WindowsAndMessaging::GetWindowRect;

    const WS_EX_NOACTIVATE: isize = 0x0800_0000;
    const WS_CAPTION: isize = 0x00C0_0000;
    let ex = crate::windows_api::WindowsApi::gwl_ex_style(hwnd).unwrap_or(0) as isize;
    if ex & WS_EX_NOACTIVATE != 0 {
        return true;
    }
    let title = crate::windows_api::WindowsApi::window_text_w(hwnd).unwrap_or_default();
    if title.starts_with("notificationtoasts") {
        return true;
    }
    unsafe {
        let h = HWND(hwnd as _);
        let style = GetWindowLongPtrW(h, GWL_STYLE) as isize;
        if style & WS_CAPTION == WS_CAPTION {
            return false;
        }
        let mut rect = Default::default();
        if GetWindowRect(h, &mut rect).is_err() {
            return false;
        }
        let mut info = MONITORINFO { cbSize: std::mem::size_of::<MONITORINFO>() as u32, ..Default::default() };
        if !GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mut info).as_bool() {
            return false;
        }
        let m = info.rcMonitor;
        rect.left <= m.left && rect.top <= m.top && rect.right >= m.right && rect.bottom >= m.bottom
    }
}
