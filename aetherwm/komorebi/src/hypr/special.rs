//! Hyprland's special workspaces (scratchpads): `movetoworkspace special:name` takes the active window out of
//! tiling and hides it; `togglespecialworkspace name` shows that workspace's windows floating, centred on the
//! focused monitor, above everything - or hides them again. While a window belongs to a special workspace komorebi
//! never manages it (`is_special`, checked in `Window::should_manage`).

use std::collections::BTreeMap;
use std::collections::HashSet;
use std::sync::Arc;
use std::sync::LazyLock;

use parking_lot::Mutex;

use crate::Window;
use crate::core::Rect;
use crate::window_manager::WindowManager;
use crate::window_manager_event::WindowManagerEvent;
use crate::windows_api::WindowsApi;
use crate::winevent_listener;

#[derive(Default)]
struct Special {
    hwnds: Vec<isize>,
    shown: bool,
}

static SPECIALS: LazyLock<Mutex<BTreeMap<String, Special>>> = LazyLock::new(|| Mutex::new(BTreeMap::new()));
static SPECIAL_HWNDS: LazyLock<Mutex<HashSet<isize>>> = LazyLock::new(|| Mutex::new(HashSet::new()));

/// komorebi must leave this window alone (it lives in a special workspace)
pub fn is_special(hwnd: isize) -> bool {
    SPECIAL_HWNDS.lock().contains(&hwnd)
}

fn name_of(name: &str) -> String {
    let name = name.trim().trim_start_matches("special").trim_start_matches(':');
    if name.is_empty() { "special".into() } else { name.to_string() }
}

/// the focused monitor's work area (komorebi's idea of it, so bars and gaps are respected)
fn focused_work_area(wm: &Arc<Mutex<WindowManager>>) -> Option<Rect> {
    let wm = wm.lock();
    let monitor = wm.focused_monitor()?;
    Some(monitor.work_area_size)
}

/// Hyprland draws a special workspace a little inset from the monitor; here: 80% of the work area, centred,
/// several windows side by side
fn place(hwnds: &[isize], area: Rect) {
    if hwnds.is_empty() {
        return;
    }
    let width = area.right * 8 / 10;
    let height = area.bottom * 8 / 10;
    let left = area.left + (area.right - width) / 2;
    let top = area.top + (area.bottom - height) / 2;
    let gap = 10;
    let each = (width - gap * (hwnds.len() as i32 - 1)) / hwnds.len() as i32;
    for (i, hwnd) in hwnds.iter().enumerate() {
        let rect = Rect { left: left + i as i32 * (each + gap), top, right: each, bottom: height };
        Window::from(*hwnd).restore();
        let _ = WindowsApi::position_window(*hwnd, &rect, true, false);
    }
    if let Some(last) = hwnds.last() {
        let _ = WindowsApi::raise_and_focus_window(*last);
    }
}

fn prune(special: &mut Special) {
    special.hwnds.retain(|h| WindowsApi::is_window(*h));
}

/// Takes the window out of its workspace now (an Unmanage event would be dropped: komorebi ignores events of
/// windows it must not manage, which a special window already is)
fn untile(wm: &Arc<Mutex<WindowManager>>, hwnd: isize) {
    let mut wm = wm.lock();
    let on_focused = wm.focused_workspace().map(|w| w.contains_window(hwnd)).unwrap_or(false);
    if on_focused {
        if let Ok(workspace) = wm.focused_workspace_mut() {
            let _ = workspace.remove_window(hwnd);
        }
        let _ = wm.update_focused_workspace(false, false);
    }
}

/// `movetoworkspace(silent) special:name` for the active window
pub fn move_active(wm: &Arc<Mutex<WindowManager>>, name: &str, silent: bool) {
    let Ok(hwnd) = WindowsApi::foreground_window() else { return };
    let name = name_of(name);
    // already in a special workspace: move it between them
    for special in SPECIALS.lock().values_mut() {
        special.hwnds.retain(|h| *h != hwnd);
    }
    SPECIAL_HWNDS.lock().insert(hwnd);
    untile(wm, hwnd);

    let mut specials = SPECIALS.lock();
    let special = specials.entry(name.clone()).or_default();
    prune(special);
    special.hwnds.push(hwnd);
    if special.shown || !silent {
        // Hyprland shows the special workspace when a window is moved there without `silent`
        special.shown = true;
        let hwnds = special.hwnds.clone();
        drop(specials);
        if let Some(area) = focused_work_area(wm) {
            place(&hwnds, area);
        }
    } else {
        Window::from(hwnd).hide();
    }
    tracing::info!("hypr: window {hwnd} -> special:{name}");
}

/// `togglespecialworkspace name`
pub fn toggle(wm: &Arc<Mutex<WindowManager>>, name: &str) {
    let name = name_of(name);
    let mut specials = SPECIALS.lock();
    let special = specials.entry(name.clone()).or_default();
    prune(special);
    special.shown = !special.shown;
    let (shown, hwnds) = (special.shown, special.hwnds.clone());
    drop(specials);
    if shown {
        if let Some(area) = focused_work_area(wm) {
            place(&hwnds, area);
        }
    } else {
        for hwnd in &hwnds {
            Window::from(*hwnd).hide();
        }
    }
    tracing::info!("hypr: special:{name} {} ({} windows)", if shown { "shown" } else { "hidden" }, hwnds.len());
}

/// `movetoworkspace N` on a window that lives in a special workspace: it goes back to tiling
pub fn release(hwnd: isize) -> bool {
    if !SPECIAL_HWNDS.lock().remove(&hwnd) {
        return false;
    }
    for special in SPECIALS.lock().values_mut() {
        special.hwnds.retain(|h| *h != hwnd);
    }
    canvas_release(hwnd);
    let _ = winevent_listener::event_tx().send(WindowManagerEvent::Manage(Window::from(hwnd)));
    true
}

/// the special workspaces and their windows (for hyprctl workspaces / the IPC)
pub fn snapshot() -> Vec<(String, Vec<isize>, bool)> {
    SPECIALS.lock().iter().map(|(n, s)| (n.clone(), s.hwnds.clone(), s.shown)).collect()
}

// ---------------------------------------------------------------- the infinite canvas (milestone 7)
// `movetocanvas` takes the active window out of tiling (like a special workspace) and marks it with the window property
// "AetherCanvas"; InfiniteDesk, started with --aether, moves only marked windows. `canvas` goes there: the focused monitor
// switches to an empty workspace, the canvas windows appear where InfiniteDesk left them, and InfiniteDesk's keys work
// while the named event Local\AetherCanvasShown is set. `canvas` again (or any `workspace`) goes back.

struct Canvas {
    hwnds: Vec<isize>,
    shown: bool,
    back: Option<usize>,
    /// (monitor, workspace) the canvas is shown on
    at: Option<(usize, usize)>,
    event: isize,
    desk: Option<std::process::Child>,
}

static CANVAS_WM: std::sync::OnceLock<Arc<Mutex<WindowManager>>> = std::sync::OnceLock::new();

/// the shown workspace changed (any way: keys, the bar, komorebic): leaving the canvas workspace hides the canvas
pub fn canvas_workspace_shown(monitor: usize, workspace: usize) {
    let leave = {
        let canvas = CANVAS.lock();
        canvas.shown && canvas.at.is_some_and(|(m, w)| m == monitor && w != workspace)
    };
    if leave && let Some(wm) = CANVAS_WM.get() {
        let wm = wm.clone();
        std::thread::spawn(move || canvas_leave(&wm, false));
    }
}

static CANVAS: LazyLock<Mutex<Canvas>> =
    LazyLock::new(|| Mutex::new(Canvas { hwnds: vec![], shown: false, back: None, at: None, event: 0, desk: None }));

fn mark(hwnd: isize, on: bool) {
    use windows::Win32::Foundation::HANDLE;
    use windows::Win32::Foundation::HWND;
    use windows::Win32::UI::WindowsAndMessaging::RemovePropW;
    use windows::Win32::UI::WindowsAndMessaging::SetPropW;
    use windows::core::w;
    unsafe {
        if on {
            let _ = SetPropW(HWND(hwnd as _), w!("AetherCanvas"), Some(HANDLE(1 as _)));
        } else {
            let _ = RemovePropW(HWND(hwnd as _), w!("AetherCanvas"));
        }
    }
}

fn signal(canvas: &mut Canvas, shown: bool) {
    use windows::Win32::Foundation::HANDLE;
    use windows::Win32::System::Threading::CreateEventW;
    use windows::Win32::System::Threading::ResetEvent;
    use windows::Win32::System::Threading::SetEvent;
    use windows::core::w;
    unsafe {
        if canvas.event == 0
            && let Ok(h) = CreateEventW(None, true, false, w!("Local\\AetherCanvasShown"))
        {
            canvas.event = h.0 as isize;
        }
        if canvas.event != 0 {
            let h = HANDLE(canvas.event as _);
            let _ = if shown { SetEvent(h) } else { ResetEvent(h) };
        }
    }
}

/// InfiniteDesk --aether (shipped in Aether next to the shell: CAELESTIA_PACKAGE_DIR\infinitedesk)
fn ensure_desk(canvas: &mut Canvas) {
    if let Some(child) = canvas.desk.as_mut()
        && matches!(child.try_wait(), Ok(None))
    {
        return;
    }
    let Ok(dir) = std::env::var("CAELESTIA_PACKAGE_DIR") else {
        tracing::warn!("hypr: canvas: CAELESTIA_PACKAGE_DIR is not set, InfiniteDesk not started");
        return;
    };
    let exe = std::path::Path::new(&dir).join("infinitedesk").join("InfiniteDesk.exe");
    match std::process::Command::new(&exe).arg("--aether").spawn() {
        Ok(child) => canvas.desk = Some(child),
        Err(error) => tracing::warn!("hypr: canvas: cannot start {}: {error}", exe.display()),
    }
}

static SLIDE_GEN: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
/// where each canvas window really sits while a slide moves it - a slide started mid-slide knows its home
static SLIDE_HOME: LazyLock<Mutex<std::collections::HashMap<isize, Rect>>> =
    LazyLock::new(|| Mutex::new(std::collections::HashMap::new()));

/// slides windows by the focused monitor's width (or height for vertical workspace animations) like the workspace
/// switch komorebi animates at the same time: `into` = they come in, otherwise they go out and are hidden (then put
/// back where they were, still hidden, so the next show starts from InfiniteDesk's positions).
/// A slide started while another one runs (Alt+C pressed quickly) takes over from where the windows are right now
/// and does the rest of the way faster - nothing waits for the old one or jumps back to its start.
fn slide(wm: &Arc<Mutex<WindowManager>>, hwnds: Vec<isize>, into: bool, forward: bool) {
    use std::sync::atomic::Ordering as O;

    use crate::animation::workspace::WorkspaceAnimationKind as K;
    let config = crate::animation::workspace::config();
    let duration = config.duration.unwrap_or(350).clamp(80, 1000);
    let vertical = matches!(config.kind, Some(K::SlideVertical) | Some(K::SlideFadeVertical));
    let Some(area) = focused_work_area(wm) else { return };
    let span = if vertical { area.bottom } else { area.right } + 40;
    // forward (to a higher workspace index): the old view leaves to the left / top, the new one comes from the right / bottom
    let sign = if forward { -1 } else { 1 };
    let generation = SLIDE_GEN.fetch_add(1, O::SeqCst) + 1;

    let mut homes = SLIDE_HOME.lock();
    // (window, its home, its displacement now - None while it is hidden / parked)
    let rects: Vec<(isize, Rect, Option<i32>)> = hwnds
        .iter()
        .filter_map(|h| {
            let now = WindowsApi::window_rect(*h).ok()?;
            let home = match homes.get(h) {
                Some(r) => *r,
                None => {
                    let mut r = now;
                    // parked off-screen (no Explorer to cloak with): its real place is the one it goes back to
                    if let Some((x, y)) = crate::park::original(*h) {
                        r.left = x;
                        r.top = y;
                    }
                    r
                }
            };
            homes.insert(*h, home);
            let shown = !crate::park::is_parked(*h) && now.left > -20000 && !crate::HIDDEN_HWNDS.lock().contains(h);
            let current = shown.then(|| if vertical { now.top - home.top } else { now.left - home.left });
            Some((*h, home, current))
        })
        .collect();
    drop(homes);
    let taking_over = rects.iter().any(|(_, _, c)| c.is_some_and(|d| d != 0));
    let duration = if taking_over { (duration * 6 / 10).max(80) } else { duration };
    let to = if into { 0 } else { sign * span };

    std::thread::spawn(move || {
        let start = std::time::Instant::now();
        let from_of = |current: Option<i32>| current.unwrap_or(if into { -sign * span } else { 0 });
        let place = |h: isize, home: &Rect, d: i32| {
            let moved = if vertical { Rect { top: home.top + d, ..*home } } else { Rect { left: home.left + d, ..*home } };
            let _ = WindowsApi::position_window_without_waiting(h, &moved, false);
        };
        if into {
            for (h, home, current) in &rects {
                if current.is_none() {
                    crate::park::forget(*h);
                    place(*h, home, from_of(None));
                    Window::from(*h).restore();
                }
            }
        }
        loop {
            if SLIDE_GEN.load(O::SeqCst) != generation {
                return; // a newer slide has these windows now
            }
            let t = (start.elapsed().as_millis() as f64 / duration as f64).min(1.0);
            let e = 1.0 - (1.0 - t).powi(3); // ease out cubic
            for (h, home, current) in &rects {
                let from = from_of(*current);
                place(*h, home, from + ((to - from) as f64 * e) as i32);
            }
            if t >= 1.0 {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(7));
        }
        if SLIDE_GEN.load(O::SeqCst) != generation {
            return;
        }
        if !into {
            for (h, home, _) in &rects {
                Window::from(*h).hide();
                if crate::park::is_parked(*h) {
                    crate::park::set_original(*h, home.left, home.top);
                } else {
                    let _ = WindowsApi::position_window_without_waiting(*h, home, false);
                }
            }
        } else if let Some((last, _, _)) = rects.last() {
            let _ = WindowsApi::raise_and_focus_window(*last);
        }
        let mut homes = SLIDE_HOME.lock();
        for (h, _, _) in &rects {
            homes.remove(h);
        }
    });
}

/// `movetocanvas`: the active window goes onto the canvas
pub fn canvas_push(wm: &Arc<Mutex<WindowManager>>) {
    let Ok(hwnd) = WindowsApi::foreground_window() else { return };
    for special in SPECIALS.lock().values_mut() {
        special.hwnds.retain(|h| *h != hwnd);
    }
    SPECIAL_HWNDS.lock().insert(hwnd);
    untile(wm, hwnd);
    mark(hwnd, true);
    let mut canvas = CANVAS.lock();
    canvas.hwnds.retain(|h| *h != hwnd && WindowsApi::is_window(*h));
    canvas.hwnds.push(hwnd);
    ensure_desk(&mut canvas);
    if !canvas.shown {
        Window::from(hwnd).hide();
    }
    tracing::info!("hypr: window {hwnd} -> canvas");
}

/// `canvas`: go to the canvas, or back to the workspace you came from
pub fn canvas_toggle(wm: &Arc<Mutex<WindowManager>>) {
    let shown = CANVAS.lock().shown;
    if shown {
        canvas_leave(wm, true);
        return;
    }
    let _ = CANVAS_WM.set(wm.clone());
    // an empty workspace on the focused monitor (a new one after the last when none is empty)
    let (monitor_idx, current, empty) = {
        let wm = wm.lock();
        let monitor_idx = wm.focused_monitor_idx();
        let Some(monitor) = wm.focused_monitor() else { return };
        let current = monitor.focused_workspace_idx();
        let workspaces = monitor.workspaces();
        let empty = workspaces
            .iter()
            .enumerate()
            .find(|(i, w)| *i != current && w.containers().is_empty() && w.floating_windows().is_empty() && w.monocle_container.is_none())
            .map(|(i, _)| i)
            .unwrap_or(workspaces.len());
        (monitor_idx, current, empty)
    };
    let _ = crate::process_command::run_command(wm, crate::core::SocketMessage::FocusWorkspaceNumber(empty));
    let mut canvas = CANVAS.lock();
    canvas.hwnds.retain(|h| WindowsApi::is_window(*h));
    canvas.back = Some(current);
    canvas.at = Some((monitor_idx, empty));
    canvas.shown = true;
    ensure_desk(&mut canvas);
    signal(&mut canvas, true);
    let hwnds = canvas.hwnds.clone();
    drop(canvas);
    slide(wm, hwnds.clone(), true, empty > current);
    canvas_announce(wm);
    tracing::info!("hypr: canvas shown ({} windows, came from workspace index {current})", hwnds.len());
}

/// hides the canvas; `go_back` = return to the workspace the canvas was entered from
pub fn canvas_leave(wm: &Arc<Mutex<WindowManager>>, go_back: bool) {
    let (back, hwnds) = {
        let mut canvas = CANVAS.lock();
        if !canvas.shown {
            return;
        }
        canvas.shown = false;
        canvas.at = None;
        signal(&mut canvas, false);
        (canvas.back.take(), canvas.hwnds.clone())
    };
    let here = wm.lock().focused_monitor().map(|m| m.focused_workspace_idx()).unwrap_or(0);
    let forward = back.map(|b| b > here).unwrap_or(true);
    slide(wm, hwnds, false, forward);
    canvas_announce(wm);
    if go_back && let Some(index) = back {
        let _ = crate::process_command::run_command(wm, crate::core::SocketMessage::FocusWorkspaceNumber(index));
    }
    tracing::info!("hypr: canvas hidden");
}

/// tells the shells (IPC) that the canvas came or went: its windows are listed on the canvas workspace while shown
fn canvas_announce(wm: &Arc<Mutex<WindowManager>>) {
    let state: crate::State = (&*wm.lock()).into();
    super::ipc::on_state(&state);
}

fn canvas_release(hwnd: isize) {
    let mut canvas = CANVAS.lock();
    if let Some(pos) = canvas.hwnds.iter().position(|h| *h == hwnd) {
        canvas.hwnds.remove(pos);
        mark(hwnd, false);
    }
}

/// at start: windows still marked as canvas windows from before a restart (the canvas list lives in memory) come
/// back onto the canvas - hidden, not tiled - instead of being lost off-screen or tiled by surprise
pub fn canvas_adopt_marked() {
    use windows::Win32::Foundation::HWND;
    use windows::Win32::Foundation::LPARAM;
    use windows::Win32::UI::WindowsAndMessaging::EnumWindows;
    use windows::Win32::UI::WindowsAndMessaging::GetPropW;
    use windows::core::BOOL;
    use windows::core::w;
    unsafe extern "system" fn found(hwnd: HWND, list: LPARAM) -> BOOL {
        let list = unsafe { &mut *(list.0 as *mut Vec<isize>) };
        if !unsafe { GetPropW(hwnd, w!("AetherCanvas")) }.0.is_null() {
            list.push(hwnd.0 as isize);
        }
        BOOL(1)
    }
    let mut marked: Vec<isize> = Vec::new();
    unsafe {
        let _ = EnumWindows(Some(found), LPARAM(&mut marked as *mut Vec<isize> as isize));
    }
    if marked.is_empty() {
        return;
    }
    let mut canvas = CANVAS.lock();
    for hwnd in &marked {
        SPECIAL_HWNDS.lock().insert(*hwnd);
        if !canvas.hwnds.contains(hwnd) {
            canvas.hwnds.push(*hwnd);
        }
        let _ = winevent_listener::event_tx().send(WindowManagerEvent::Unmanage(Window::from(*hwnd)));
        Window::from(*hwnd).hide();
    }
    tracing::info!("hypr: {} window(s) back on the canvas after a restart", marked.len());
}

/// the canvas windows and where the canvas is shown (monitor, workspace), None while hidden (for the IPC)
pub fn canvas_snapshot() -> (Vec<isize>, Option<(usize, usize)>) {
    let canvas = CANVAS.lock();
    (canvas.hwnds.clone(), if canvas.shown { canvas.at } else { None })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names() {
        assert_eq!(name_of("magic"), "magic");
        assert_eq!(name_of("special:magic"), "magic");
        assert_eq!(name_of(""), "special");
        assert_eq!(name_of("special"), "special");
    }
}
