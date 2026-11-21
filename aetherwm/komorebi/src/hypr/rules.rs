//! Hyprland's window rules and workspace rules.
//!
//! Window rules (`windowrule` / `windowrulev2`, old and new syntax, and hyprland.lua's hl.window_rule) apply when a
//! window opens: float, tile, workspace N [silent], monitor N, size W H, move X Y, center, fullscreen, maximize.
//! Matching: `class` (the exe name as written, lower case, or the Win32 window class), `title`, `initialClass`,
//! `initialTitle`, with Hyprland's `negative:` prefix. Regexes match anywhere unless anchored, like Hyprland.
//!
//! Workspace rules (`workspace = 3, gapsin:2, gapsout:8, defaultName:web, on-created-empty:kitty`): gaps and names
//! apply when the config loads; on-created-empty runs when you go to that workspace while it is empty.

use std::sync::Arc;
use std::sync::LazyLock;
use std::sync::OnceLock;

use crossbeam_channel::Sender;
use parking_lot::Mutex;
use parking_lot::RwLock;
use regex::Regex;

use super::conf::HyprConf;
use super::conf::WindowRule;
use crate::Window;
use crate::core::Rect;
use crate::core::SocketMessage;
use crate::window_manager::WindowManager;
use crate::windows_api::WindowsApi;

#[derive(Debug, Clone)]
struct Matcher {
    field: String,
    regex: Regex,
    negative: bool,
}

#[derive(Debug, Clone)]
struct Compiled {
    effect: String,
    matchers: Vec<Matcher>,
}

#[derive(Debug, Clone, Default)]
pub struct WorkspaceRule {
    pub id: i64,
    pub gaps_in: Option<i32>,
    pub gaps_out: Option<i32>,
    pub name: Option<String>,
    pub on_created_empty: Option<String>,
}

static RULES: RwLock<Vec<Compiled>> = RwLock::new(Vec::new());
static WORKSPACE_RULES: RwLock<Vec<WorkspaceRule>> = RwLock::new(Vec::new());
static NEW_WINDOWS: OnceLock<Sender<isize>> = OnceLock::new();
/// the last workspace on-created-empty ran for (so it runs once per visit)
static LAST_EMPTY: LazyLock<Mutex<Option<i64>>> = LazyLock::new(|| Mutex::new(None));

fn compile(rule: &WindowRule) -> Option<Compiled> {
    let mut matchers = Vec::new();
    for (field, pattern) in &rule.matchers {
        let (negative, pattern) = match pattern.strip_prefix("negative:") {
            Some(rest) => (true, rest),
            None => (false, pattern.as_str()),
        };
        let field = field.trim().to_ascii_lowercase().replace('_', "");
        // window state matchers (floating:1, fullscreen:1, workspace:...) are not known when a window opens
        if !matches!(field.as_str(), "class" | "title" | "initialclass" | "initialtitle") {
            return None;
        }
        let regex = Regex::new(pattern).ok()?;
        matchers.push(Matcher { field, regex, negative });
    }
    Some(Compiled { effect: rule.rule.trim().to_string(), matchers })
}

pub fn set_rules(window_rules: &[WindowRule], workspace_rules: Vec<WorkspaceRule>) -> usize {
    let compiled: Vec<Compiled> = window_rules.iter().filter_map(compile).collect();
    let count = compiled.len();
    *RULES.write() = compiled;
    *WORKSPACE_RULES.write() = workspace_rules;
    count
}

/// `workspace = 3, gapsin:2, gapsout:8, defaultName:web, on-created-empty:kitty, monitor:DP-1, default:true`
pub fn parse_workspace_rules(conf: &HyprConf) -> Vec<WorkspaceRule> {
    let mut out = Vec::new();
    for line in &conf.workspaces {
        let mut parts = line.split(',').map(str::trim);
        let Some(Ok(id)) = parts.next().map(|w| w.trim_start_matches("name:").parse::<i64>()) else { continue };
        let mut rule = WorkspaceRule { id, ..Default::default() };
        for part in parts {
            let Some((key, value)) = part.split_once(':') else { continue };
            match key.trim() {
                "gapsin" => rule.gaps_in = value.trim().split_whitespace().next().and_then(|v| v.parse().ok()),
                "gapsout" => rule.gaps_out = value.trim().split_whitespace().next().and_then(|v| v.parse().ok()),
                "defaultName" => rule.name = Some(value.trim().to_string()),
                "on-created-empty" => rule.on_created_empty = Some(value.trim().to_string()),
                _ => {}
            }
        }
        out.push(rule);
    }
    out
}

/// the effects of the rules that match a window, in order
pub fn effects_for(class_candidates: &[String], title: &str) -> Vec<String> {
    let rules = RULES.read();
    rules
        .iter()
        .filter(|rule| {
            !rule.matchers.is_empty()
                && rule.matchers.iter().all(|m| {
                    let hit = match m.field.as_str() {
                        "class" | "initialclass" => class_candidates.iter().any(|c| m.regex.is_match(c)),
                        _ => m.regex.is_match(title),
                    };
                    hit != m.negative
                })
        })
        .map(|rule| rule.effect.clone())
        .collect()
}

/// what a window's class may be called in a rule: exe name as written (Taskmgr), lower case (taskmgr), window class
fn class_candidates(window: Window) -> Vec<String> {
    let mut out = Vec::new();
    if let Ok(exe) = window.exe() {
        let stem = exe.strip_suffix(".exe").or_else(|| exe.strip_suffix(".EXE")).unwrap_or(&exe).to_string();
        out.push(stem.to_ascii_lowercase());
        out.push(stem);
    }
    if let Ok(class) = window.class() {
        out.push(class);
    }
    out
}

/// a window opened (ipc::on_state saw it): its rules run on the rules thread
pub fn window_opened(hwnd: isize) {
    if RULES.read().is_empty() {
        return;
    }
    if let Some(tx) = NEW_WINDOWS.get() {
        let _ = tx.send(hwnd);
    }
}

pub fn start(wm: Arc<Mutex<WindowManager>>) {
    if NEW_WINDOWS.get().is_some() {
        return;
    }
    let (tx, rx) = crossbeam_channel::unbounded::<isize>();
    let _ = NEW_WINDOWS.set(tx);
    std::thread::Builder::new()
        .name("hypr-rules".into())
        .spawn(move || {
            for hwnd in rx {
                // let komorebi finish placing it first
                std::thread::sleep(std::time::Duration::from_millis(120));
                apply(&wm, hwnd);
            }
        })
        .expect("could not start the window rules thread");
}

fn number(value: &str, whole: i32) -> Option<i32> {
    let value = value.trim();
    if let Some(percent) = value.strip_suffix('%') {
        return percent.trim().parse::<f32>().ok().map(|p| (whole as f32 * p / 100.0).round() as i32);
    }
    value.parse::<f32>().ok().map(|v| v.round() as i32)
}

fn apply(wm: &Arc<Mutex<WindowManager>>, hwnd: isize) {
    if !WindowsApi::is_window(hwnd) {
        return;
    }
    let window = Window::from(hwnd);
    let effects = effects_for(&class_candidates(window), &window.title().unwrap_or_default());
    if effects.is_empty() {
        return;
    }
    tracing::info!("hypr: rules for {hwnd} ({}): {effects:?}", window.exe().unwrap_or_default());

    // komorebi's commands act on the focused window: a new window normally is; make sure
    if WindowsApi::foreground_window().ok() != Some(hwnd) {
        let _ = WindowsApi::raise_and_focus_window(hwnd);
        std::thread::sleep(std::time::Duration::from_millis(60));
    }
    let floating_now = || {
        let wm = wm.lock();
        wm.focused_workspace().map(|w| w.floating_windows().iter().any(|f| f.hwnd == hwnd)).unwrap_or(false)
    };
    let work_area = || wm.lock().focused_monitor().map(|m| m.work_area_size).unwrap_or_default();

    let mut size: Option<(i32, i32)> = None;
    let mut position: Option<(i32, i32)> = None;
    let mut center = false;
    for effect in &effects {
        let mut words = effect.split_whitespace();
        let name = words.next().unwrap_or("");
        let rest: Vec<&str> = words.collect();
        match name {
            "float" | "setfloating" => {
                if !floating_now() {
                    let _ = crate::process_command::run_command(wm, SocketMessage::ToggleFloat);
                }
            }
            "tile" | "settiled" => {
                if floating_now() {
                    let _ = crate::process_command::run_command(wm, SocketMessage::ToggleFloat);
                }
            }
            "workspace" => {
                let target = rest.first().copied().unwrap_or("");
                let silent = rest.get(1) == Some(&"silent");
                let dispatcher = if silent { "movetoworkspacesilent" } else { "movetoworkspace" };
                super::ipc::run_dispatch(wm, dispatcher, target);
            }
            "monitor" => {
                if let Some(n) = rest.first().and_then(|n| n.parse::<usize>().ok()) {
                    let _ = crate::process_command::run_command(wm, SocketMessage::MoveContainerToMonitorNumber(n));
                }
            }
            "fullscreen" => super::toggle_true_fullscreen(wm),
            "maximize" => {
                let _ = crate::process_command::run_command(wm, SocketMessage::ToggleMonocle);
            }
            "size" => {
                let area = work_area();
                if let (Some(w), Some(h)) = (rest.first().and_then(|v| number(v, area.right)), rest.get(1).and_then(|v| number(v, area.bottom))) {
                    size = Some((w, h));
                }
            }
            "move" => {
                let area = work_area();
                if let (Some(x), Some(y)) = (rest.first().and_then(|v| number(v, area.right)), rest.get(1).and_then(|v| number(v, area.bottom))) {
                    position = Some((area.left + x, area.top + y));
                }
            }
            "center" => center = true,
            other => tracing::info!("hypr: window rule '{other}' is not done on Windows yet"),
        }
    }

    // size / move / center: for floating windows (tiled ones get their place from the layout). komorebi places a
    // window it just floated itself, a moment later - so wait for that, then set ours
    if (size.is_some() || position.is_some() || center) && floating_now() {
        std::thread::sleep(std::time::Duration::from_millis(250));
        let area = work_area();
        let current = WindowsApi::window_rect(hwnd).unwrap_or_default();
        let (w, h) = size.unwrap_or((current.right, current.bottom));
        let (x, y) = if center {
            (area.left + (area.right - w) / 2, area.top + (area.bottom - h) / 2)
        } else {
            position.unwrap_or((current.left, current.top))
        };
        let _ = WindowsApi::position_window(hwnd, &Rect { left: x, top: y, right: w, bottom: h }, false, true);
    }
}

/// gaps and names of workspace rules, as komorebi operations (ids: 1-10 first monitor, 11-20 second, ...)
pub fn workspace_rule_messages() -> Vec<SocketMessage> {
    let mut out = Vec::new();
    for rule in WORKSPACE_RULES.read().iter() {
        let Some((m, w)) = super::ipc::workspace_of(rule.id) else { continue };
        if let Some(gap) = rule.gaps_in {
            out.push(SocketMessage::ContainerPadding(m, w, gap));
        }
        if let Some(gap) = rule.gaps_out {
            out.push(SocketMessage::WorkspacePadding(m, w, gap));
        }
        if let Some(name) = &rule.name {
            out.push(SocketMessage::WorkspaceName(m, w, name.clone()));
        }
    }
    out
}

/// the focused workspace changed (ipc::on_state): run on-created-empty when it is empty
pub fn workspace_shown(id: i64, empty: bool) {
    let mut last = LAST_EMPTY.lock();
    if !empty {
        if *last == Some(id) {
            *last = None;
        }
        return;
    }
    if *last == Some(id) {
        return;
    }
    let command = WORKSPACE_RULES.read().iter().find(|r| r.id == id).and_then(|r| r.on_created_empty.clone());
    if let Some(command) = command {
        *last = Some(id);
        super::binds::exec(&command);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn matching() {
        let conf = HyprConf::parse_str(
            "windowrule = float, class:^(Calculator|SnippingTool|Taskmgr)$\nwindowrulev2 = workspace 3 silent, class:^(discord)$\nwindowrulev2 = size 50% 50%, title:^(Picture-in-Picture)$, class:negative:^(firefox)$\nwindowrulev2 = center, floating:1, class:.*\n",
            std::path::Path::new("."),
        );
        assert_eq!(set_rules(&conf.window_rules, vec![]), 3); // floating:1 is skipped
        let taskmgr = vec!["taskmgr".to_string(), "Taskmgr".to_string(), "TaskManagerWindow".to_string()];
        assert_eq!(effects_for(&taskmgr, "Task Manager"), vec!["float".to_string()]);
        let discord = vec!["discord".to_string(), "Discord".to_string(), "Chrome_WidgetWin_1".to_string()];
        assert_eq!(effects_for(&discord, "#general"), vec!["workspace 3 silent".to_string()]);
        let zen = vec!["zen".to_string(), "Zen".to_string(), "MozillaWindowClass".to_string()];
        assert_eq!(effects_for(&zen, "Picture-in-Picture"), vec!["size 50% 50%".to_string()]);
        let firefox = vec!["firefox".to_string()];
        assert!(effects_for(&firefox, "Picture-in-Picture").is_empty());
    }

    #[test]
    fn workspace_rules_parse() {
        let conf = HyprConf::parse_str("workspace = 3, gapsin:2, gapsout:8 8 8 8, defaultName:web, on-created-empty:wt.exe\nworkspace = special:magic, gapsout:50\n", std::path::Path::new("."));
        let rules = parse_workspace_rules(&conf);
        assert_eq!(rules.len(), 1);
        assert_eq!((rules[0].id, rules[0].gaps_in, rules[0].gaps_out), (3, Some(2), Some(8)));
        assert_eq!(rules[0].name.as_deref(), Some("web"));
        assert_eq!(rules[0].on_created_empty.as_deref(), Some("wt.exe"));
        assert_eq!(number("50%", 1000), Some(500));
    }
}

/// `hyprctl keyword windowrule(v2) ...`: adds rules at runtime (until the next reload)
pub fn add_rules(window_rules: &[WindowRule]) -> usize {
    let compiled: Vec<Compiled> = window_rules.iter().filter_map(compile).collect();
    let count = compiled.len();
    RULES.write().extend(compiled);
    count
}
