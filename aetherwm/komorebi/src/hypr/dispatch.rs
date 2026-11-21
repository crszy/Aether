//! Hyprland dispatchers -> what the window manager does.
//!
//! `translate` is pure (no window manager needed), so every mapping is unit-tested; `binds.rs` and the
//! IPC's `dispatch` run the result. Workspaces are Hyprland's 1-based numbers; komorebi's are 0-based
//! per monitor (global workspaces across monitors come with milestone 2).

use crate::core::Axis;
use crate::core::CycleDirection;
use crate::core::OperationDirection;
use crate::core::Sizing;
use crate::core::SocketMessage;

#[derive(Debug, Clone, PartialEq)]
pub enum MouseDrag {
    Move,
    Resize,
}

#[derive(Debug, Clone)]
pub enum Action {
    /// komorebi operations, in order
    Wm(Vec<SocketMessage>),
    /// run a command line (Hyprland runs it through `sh -c`; here through the Windows shell)
    Exec(String),
    /// enter a submap (None = back to the global map)
    Submap(Option<String>),
    /// `bindm`: drag the window under the cursor while the button is held
    Drag(MouseDrag),
    /// `togglespecialworkspace name` (scratchpad); empty name = "special"
    ToggleSpecial(String),
    /// `movetoworkspace(silent) special:name`
    MoveToSpecial { name: String, silent: bool },
    /// a dispatcher that exists in Hyprland but is not done yet / cannot be done on Windows
    Unsupported(String),
    /// nothing to do (eg. `pass`)
    Nothing,
}

// SocketMessage has no PartialEq; compare through its serde form (tests and dedup only)
impl PartialEq for Action {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (Action::Wm(a), Action::Wm(b)) => serde_json::to_string(a).ok() == serde_json::to_string(b).ok(),
            (Action::Exec(a), Action::Exec(b)) => a == b,
            (Action::Submap(a), Action::Submap(b)) => a == b,
            (Action::Drag(a), Action::Drag(b)) => a == b,
            (Action::ToggleSpecial(a), Action::ToggleSpecial(b)) => a == b,
            (Action::MoveToSpecial { name: a, silent: x }, Action::MoveToSpecial { name: b, silent: y }) => a == b && x == y,
            (Action::Unsupported(a), Action::Unsupported(b)) => a == b,
            (Action::Nothing, Action::Nothing) => true,
            _ => false,
        }
    }
}

fn direction(arg: &str) -> Option<OperationDirection> {
    match arg.trim().to_ascii_lowercase().as_str() {
        "l" | "left" => Some(OperationDirection::Left),
        "r" | "right" => Some(OperationDirection::Right),
        "u" | "t" | "up" | "top" => Some(OperationDirection::Up),
        "d" | "b" | "down" | "bottom" => Some(OperationDirection::Down),
        _ => None,
    }
}

/// Hyprland workspace argument -> komorebi messages (`move` = take the active window along)
fn workspace_messages(arg: &str, mode: WorkspaceMode) -> Option<Vec<SocketMessage>> {
    let arg = arg.trim();
    // "name:foo" -> named workspace
    if let Some(name) = arg.strip_prefix("name:") {
        let name = name.to_string();
        return Some(vec![match mode {
            WorkspaceMode::Focus => SocketMessage::FocusNamedWorkspace(name),
            WorkspaceMode::Move => SocketMessage::MoveContainerToNamedWorkspace(name),
            WorkspaceMode::Send => SocketMessage::SendContainerToNamedWorkspace(name),
        }]);
    }
    if arg == "previous" || arg == "previous_per_monitor" {
        return Some(vec![match mode {
            WorkspaceMode::Focus => SocketMessage::FocusLastWorkspace,
            WorkspaceMode::Move => SocketMessage::MoveContainerToLastWorkspace,
            WorkspaceMode::Send => SocketMessage::SendContainerToLastWorkspace,
        }]);
    }
    if arg == "empty" {
        return match mode {
            WorkspaceMode::Focus => Some(vec![SocketMessage::CycleFocusEmptyWorkspace(CycleDirection::Next)]),
            _ => None,
        };
    }
    // relative: "+1", "-1", "e+1", "r-1", "m+1" (all treated as next/previous on this monitor for now)
    let relative = arg.trim_start_matches(['e', 'r', 'm']);
    if let Some(step) = relative.strip_prefix('+').and_then(|n| n.parse::<usize>().ok()) {
        return Some(cycle(CycleDirection::Next, step, mode));
    }
    if let Some(step) = relative.strip_prefix('-').and_then(|n| n.parse::<usize>().ok()) {
        return Some(cycle(CycleDirection::Previous, step, mode));
    }
    // absolute: "3" (Hyprland numbers start at 1)
    let number = arg.parse::<usize>().ok().filter(|n| *n >= 1)?;
    let index = number - 1;
    Some(vec![match mode {
        WorkspaceMode::Focus => SocketMessage::FocusWorkspaceNumber(index),
        WorkspaceMode::Move => SocketMessage::MoveContainerToWorkspaceNumber(index),
        WorkspaceMode::Send => SocketMessage::SendContainerToWorkspaceNumber(index),
    }])
}

#[derive(Clone, Copy)]
enum WorkspaceMode {
    Focus,
    Move,
    Send,
}

fn cycle(direction: CycleDirection, step: usize, mode: WorkspaceMode) -> Vec<SocketMessage> {
    let one = match mode {
        WorkspaceMode::Focus => SocketMessage::CycleFocusWorkspace(direction),
        WorkspaceMode::Move => SocketMessage::CycleMoveContainerToWorkspace(direction),
        WorkspaceMode::Send => SocketMessage::CycleSendContainerToWorkspace(direction),
    };
    vec![one; step.max(1)]
}

/// `resizeactive 40 -20` (pixels, may be `exact` / percentages - only relative pixels for now)
fn resize(arg: &str) -> Option<Vec<SocketMessage>> {
    let mut parts = arg.split_whitespace();
    let x: i32 = parts.next()?.trim_end_matches("px").parse().ok()?;
    let y: i32 = parts.next().unwrap_or("0").trim_end_matches("px").parse().ok()?;
    let mut out = Vec::new();
    if x != 0 {
        out.push(SocketMessage::ResizeDelta(x.abs()));
        // along the axis, like Hyprland (an edge resize does nothing when that edge has no neighbour)
        out.push(SocketMessage::ResizeWindowAxis(Axis::Horizontal, if x > 0 { Sizing::Increase } else { Sizing::Decrease }));
    }
    if y != 0 {
        out.push(SocketMessage::ResizeDelta(y.abs()));
        out.push(SocketMessage::ResizeWindowAxis(Axis::Vertical, if y > 0 { Sizing::Increase } else { Sizing::Decrease }));
    }
    (!out.is_empty()).then_some(out)
}

pub fn translate(dispatcher: &str, arg: &str) -> Action {
    let arg = arg.trim();
    let wm = |m: SocketMessage| Action::Wm(vec![m]);
    match dispatcher.trim() {
        "exec" | "execr" => Action::Exec(strip_exec_rules(arg).to_string()),
        "pass" | "sendshortcut" | "sendkeystate" => Action::Nothing,
        "killactive" | "forcekillactive" => wm(SocketMessage::Close),
        "closewindow" | "killwindow" if arg.is_empty() || arg == "activewindow" => wm(SocketMessage::Close),
        "togglefloating" | "setfloating" | "settiled" => wm(SocketMessage::ToggleFloat),
        // fullscreen 0 = real fullscreen, 1 = maximize (keeps gaps and bars), 2 = fullscreen state only
        "fullscreen" => match arg {
            "1" => wm(SocketMessage::ToggleMonocle),
            _ => wm(SocketMessage::ToggleMaximize),
        },
        "fullscreenstate" => wm(SocketMessage::ToggleMaximize),
        "movefocus" => direction(arg).map_or_else(|| Action::Unsupported(format!("movefocus {arg}")), |d| wm(SocketMessage::FocusWindow(d))),
        "movewindow" => {
            if let Some(mon) = arg.strip_prefix("mon:") {
                return match mon.trim() {
                    "l" | "u" | "-1" => wm(SocketMessage::CycleMoveContainerToMonitor(CycleDirection::Previous)),
                    "r" | "d" | "+1" => wm(SocketMessage::CycleMoveContainerToMonitor(CycleDirection::Next)),
                    n => n.parse::<usize>().map_or_else(|_| Action::Unsupported(format!("movewindow {arg}")), |n| wm(SocketMessage::MoveContainerToMonitorNumber(n))),
                };
            }
            match direction(arg) {
                Some(d) => wm(SocketMessage::MoveWindow(d)),
                None if arg.is_empty() => Action::Drag(MouseDrag::Move), // bindm ..., movewindow
                None => Action::Unsupported(format!("movewindow {arg}")),
            }
        }
        "resizewindow" if arg.is_empty() => Action::Drag(MouseDrag::Resize),
        "swapwindow" => direction(arg).map_or_else(|| Action::Unsupported(format!("swapwindow {arg}")), |d| wm(SocketMessage::MoveWindow(d))),
        "resizeactive" => resize(arg).map_or_else(|| Action::Unsupported(format!("resizeactive {arg}")), Action::Wm),
        "cyclenext" => wm(SocketMessage::CycleFocusWindow(if arg.starts_with("prev") { CycleDirection::Previous } else { CycleDirection::Next })),
        "swapnext" => wm(SocketMessage::CycleMoveWindow(if arg.starts_with("prev") { CycleDirection::Previous } else { CycleDirection::Next })),
        "workspace" => {
            if let Some(name) = arg.strip_prefix("special") {
                return Action::ToggleSpecial(name.trim_start_matches(':').to_string());
            }
            workspace_messages(arg, WorkspaceMode::Focus).map_or_else(|| Action::Unsupported(format!("workspace {arg}")), Action::Wm)
        }
        "movetoworkspace" | "movetoworkspacesilent" => {
            let silent = dispatcher.ends_with("silent");
            // "3,address:0x..." targets another window: only the active one for now
            let target = arg.split(',').next().unwrap_or(arg).trim();
            if let Some(name) = target.strip_prefix("special") {
                return Action::MoveToSpecial { name: name.trim_start_matches(':').to_string(), silent };
            }
            let mode = if silent { WorkspaceMode::Send } else { WorkspaceMode::Move };
            workspace_messages(target, mode).map_or_else(|| Action::Unsupported(format!("{dispatcher} {arg}")), Action::Wm)
        }
        "togglespecialworkspace" => Action::ToggleSpecial(arg.to_string()),
        "focusmonitor" => match arg {
            "l" | "u" | "-1" => wm(SocketMessage::CycleFocusMonitor(CycleDirection::Previous)),
            "r" | "d" | "+1" => wm(SocketMessage::CycleFocusMonitor(CycleDirection::Next)),
            n => n.parse::<usize>().map_or_else(|_| Action::Unsupported(format!("focusmonitor {arg}")), |n| wm(SocketMessage::FocusMonitorNumber(n))),
        },
        "movecurrentworkspacetomonitor" => match arg {
            "l" | "u" | "-1" => wm(SocketMessage::CycleMoveWorkspaceToMonitor(CycleDirection::Previous)),
            "r" | "d" | "+1" => wm(SocketMessage::CycleMoveWorkspaceToMonitor(CycleDirection::Next)),
            n => n.parse::<usize>().map_or_else(|_| Action::Unsupported(format!("{dispatcher} {arg}")), |n| wm(SocketMessage::MoveWorkspaceToMonitorNumber(n))),
        },
        // dwindle's per-node split comes with the real dwindle layout (milestone 2); until then flip the layout
        "togglesplit" => wm(SocketMessage::FlipLayout(Axis::HorizontalAndVertical)),
        "swapsplit" => wm(SocketMessage::FlipLayout(Axis::Horizontal)),
        "layoutmsg" => match arg.split_whitespace().next().unwrap_or("") {
            "togglesplit" => wm(SocketMessage::FlipLayout(Axis::HorizontalAndVertical)),
            "swapwithmaster" | "focusmaster" => wm(SocketMessage::Promote),
            "cyclenext" => wm(SocketMessage::CycleFocusWindow(CycleDirection::Next)),
            "cycleprev" => wm(SocketMessage::CycleFocusWindow(CycleDirection::Previous)),
            _ => Action::Unsupported(format!("layoutmsg {arg}")),
        },
        "submap" => Action::Submap(match arg {
            "reset" | "" => None,
            name => Some(name.to_string()),
        }),
        // Aether extras (no Hyprland equivalent; hyprexpo's name works too): the fork's workspace overview, layouts
        "overview" | "hyprexpo:expo" => wm(SocketMessage::ToggleOverview),
        "setlayout" => match arg.to_ascii_lowercase().as_str() {
            "bsp" | "dwindle" => wm(SocketMessage::ChangeLayout(crate::core::DefaultLayout::BSP)),
            "master" | "vstack" | "verticalstack" => wm(SocketMessage::ChangeLayout(crate::core::DefaultLayout::VerticalStack)),
            "scrolling" => wm(SocketMessage::ChangeLayout(crate::core::DefaultLayout::Scrolling)),
            "columns" => wm(SocketMessage::ChangeLayout(crate::core::DefaultLayout::Columns)),
            "rows" => wm(SocketMessage::ChangeLayout(crate::core::DefaultLayout::Rows)),
            "grid" => wm(SocketMessage::ChangeLayout(crate::core::DefaultLayout::Grid)),
            _ => Action::Unsupported(format!("setlayout {arg}")),
        },
        "exit" => wm(SocketMessage::Stop),
        "forcerendererreload" => wm(SocketMessage::Retile),
        "minimize" => wm(SocketMessage::Minimize),
        other => Action::Unsupported(format!("{other} {arg}").trim().to_string()),
    }
}

/// `exec [float; workspace 2] kitty` -> `kitty` (the inline rules come with milestone 4)
fn strip_exec_rules(arg: &str) -> &str {
    if arg.starts_with('[')
        && let Some(end) = arg.find(']')
    {
        return arg[end + 1..].trim();
    }
    arg
}

#[cfg(test)]
mod tests {
    use super::*;

    fn wm(m: Vec<SocketMessage>) -> Action {
        Action::Wm(m)
    }

    #[test]
    fn the_users_binds() {
        assert_eq!(translate("exec", "wt.exe"), Action::Exec("wt.exe".into()));
        assert_eq!(translate("killactive", ""), wm(vec![SocketMessage::Close]));
        assert_eq!(translate("togglefloating", ""), wm(vec![SocketMessage::ToggleFloat]));
        assert_eq!(translate("fullscreen", "1"), wm(vec![SocketMessage::ToggleMonocle]));
        assert_eq!(translate("fullscreen", "0"), wm(vec![SocketMessage::ToggleMaximize]));
        assert_eq!(translate("movefocus", "l"), wm(vec![SocketMessage::FocusWindow(OperationDirection::Left)]));
        assert_eq!(translate("swapwindow", "d"), wm(vec![SocketMessage::MoveWindow(OperationDirection::Down)]));
        assert_eq!(
            translate("resizeactive", "-40 0"),
            wm(vec![SocketMessage::ResizeDelta(40), SocketMessage::ResizeWindowAxis(Axis::Horizontal, Sizing::Decrease)])
        );
        assert_eq!(translate("movewindow", ""), Action::Drag(MouseDrag::Move));
        assert_eq!(translate("resizewindow", ""), Action::Drag(MouseDrag::Resize));
        assert_eq!(translate("workspace", "1"), wm(vec![SocketMessage::FocusWorkspaceNumber(0)]));
        assert_eq!(translate("movetoworkspace", "5"), wm(vec![SocketMessage::MoveContainerToWorkspaceNumber(4)]));
        assert_eq!(translate("togglespecialworkspace", "magic"), Action::ToggleSpecial("magic".into()));
        assert_eq!(translate("movetoworkspace", "special:magic"), Action::MoveToSpecial { name: "magic".into(), silent: false });
    }

    #[test]
    fn workspace_forms() {
        assert_eq!(translate("workspace", "+1"), wm(vec![SocketMessage::CycleFocusWorkspace(CycleDirection::Next)]));
        assert_eq!(translate("workspace", "e-1"), wm(vec![SocketMessage::CycleFocusWorkspace(CycleDirection::Previous)]));
        assert_eq!(translate("workspace", "previous"), wm(vec![SocketMessage::FocusLastWorkspace]));
        assert_eq!(translate("workspace", "name:web"), wm(vec![SocketMessage::FocusNamedWorkspace("web".into())]));
        assert_eq!(translate("movetoworkspacesilent", "2"), wm(vec![SocketMessage::SendContainerToWorkspaceNumber(1)]));
        assert_eq!(translate("workspace", "special:term"), Action::ToggleSpecial("term".into()));
        assert!(matches!(translate("workspace", "0"), Action::Unsupported(_)));
    }

    #[test]
    fn monitors_submaps_misc() {
        assert_eq!(translate("focusmonitor", "+1"), wm(vec![SocketMessage::CycleFocusMonitor(CycleDirection::Next)]));
        assert_eq!(translate("focusmonitor", "2"), wm(vec![SocketMessage::FocusMonitorNumber(2)]));
        assert_eq!(translate("movewindow", "mon:l"), wm(vec![SocketMessage::CycleMoveContainerToMonitor(CycleDirection::Previous)]));
        assert_eq!(translate("submap", "resize"), Action::Submap(Some("resize".into())));
        assert_eq!(translate("submap", "reset"), Action::Submap(None));
        assert_eq!(translate("exec", "[float] kitty --hold"), Action::Exec("kitty --hold".into()));
        assert_eq!(translate("cyclenext", "prev"), wm(vec![SocketMessage::CycleFocusWindow(CycleDirection::Previous)]));
        assert!(matches!(translate("pin", ""), Action::Unsupported(_)));
        assert_eq!(translate("overview", ""), wm(vec![SocketMessage::ToggleOverview]));
        assert_eq!(translate("setlayout", "scrolling"), wm(vec![SocketMessage::ChangeLayout(crate::core::DefaultLayout::Scrolling)]));
    }
}
