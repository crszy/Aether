//! Hyprland's looks on Windows: `animations {}` (beziers, the windows / workspaces leaves and their styles) onto the
//! fork's animation engine, `decoration:rounding` as Windows 11 corners, `decoration:inactive_opacity` through
//! komorebi's transparency, and the border style. Blur and shadows are drawn by Windows itself (noted, not set).

use std::sync::Arc;
use std::sync::atomic::AtomicI32;
use std::sync::atomic::Ordering;

use parking_lot::Mutex;

use super::conf::HyprConf;
use crate::animation::workspace::WindowAnimationConfig;
use crate::animation::workspace::WorkspaceAnimationKind;
use crate::core::BorderStyle;
use crate::core::SocketMessage;
use crate::window_manager::WindowManager;
use crate::windows_api::WindowsApi;

/// a runtime "keyword animations:enabled" (game mode) until the next reload
pub static ANIM_OVERRIDE: std::sync::Mutex<Option<bool>> = std::sync::Mutex::new(None);

/// DWM corner preference from decoration:rounding (-1 = leave windows as they are)
static CORNERS: AtomicI32 = AtomicI32::new(-1);

/// Hyprland's built-in curves
fn builtin_bezier(name: &str) -> Option<[f64; 4]> {
    Some(match name {
        "default" => [0.0, 0.75, 0.15, 1.0],
        "linear" => [0.0, 0.0, 1.0, 1.0],
        "easeInOutCubic" => [0.65, 0.05, 0.36, 1.0],
        "easeOutQuint" => [0.23, 1.0, 0.32, 1.0],
        "almostLinear" => [0.5, 0.5, 0.75, 1.0],
        "quick" => [0.15, 0.0, 0.1, 1.0],
        _ => return None,
    })
}

fn bezier(conf: &HyprConf, name: &str) -> Option<[f64; 4]> {
    conf.beziers
        .iter()
        .rev()
        .find(|b| b.name == name)
        .map(|b| [b.points[0] as f64, b.points[1] as f64, b.points[2] as f64, b.points[3] as f64])
        .or_else(|| builtin_bezier(name))
}

/// a leaf's settings, falling back like Hyprland: windowsIn -> windows -> global
fn leaf<'a>(conf: &'a HyprConf, names: &[&str]) -> Option<&'a super::conf::Animation> {
    for name in names {
        if let Some(a) = conf.animations.iter().rev().find(|a| a.name == *name) {
            return Some(a);
        }
    }
    None
}

fn workspace_kind(style: &str) -> Option<WorkspaceAnimationKind> {
    let style = style.split_whitespace().next().unwrap_or("");
    Some(match style {
        "slide" => WorkspaceAnimationKind::Slide,
        "slidevert" => WorkspaceAnimationKind::SlideVertical,
        "fade" => WorkspaceAnimationKind::Fade,
        "slidefade" => WorkspaceAnimationKind::SlideFade,
        "slidefadevert" => WorkspaceAnimationKind::SlideFadeVertical,
        _ => return None,
    })
}

/// Hyprland's speed is in deciseconds (1 = 100 ms)
fn duration(speed: f32) -> u64 {
    (speed.max(0.1) * 100.0).round() as u64
}

pub fn apply(wm: &Arc<Mutex<WindowManager>>, conf: &HyprConf) {
    // ---- animations ----
    // not in the file = on (Hyprland's default) - so a reload undoes a runtime "animations:enabled 0" (game mode,
    // which a fullscreen screenshot overlay like ShareX's turns on for a moment) instead of leaving them off
    let enabled = conf.get_bool("animations:enabled").or(*ANIM_OVERRIDE.lock().unwrap()).unwrap_or(true);
    let _ = crate::process_command::run_command(wm, SocketMessage::Animation(enabled, None));
    if !conf.animations.is_empty() {
        let mut workspaces = crate::animation::workspace::config();
        if let Some(a) = leaf(conf, &["workspaces", "workspacesIn", "global"]) {
            workspaces.enabled = Some(a.enabled);
            workspaces.duration = Some(duration(a.speed));
            if let Some(curve) = bezier(conf, &a.curve) {
                workspaces.bezier = Some(curve);
            }
            if let Some(kind) = workspace_kind(&a.style) {
                workspaces.kind = Some(kind);
            }
        }
        crate::animation::workspace::set_config(Some(workspaces));

        let mut windows = crate::animation::workspace::window_config().unwrap_or(WindowAnimationConfig::default());
        if let Some(a) = leaf(conf, &["windowsMove", "windows", "global"]) {
            windows.enabled = Some(a.enabled);
            windows.duration = Some(duration(a.speed));
            if let Some(curve) = bezier(conf, &a.curve) {
                windows.bezier = Some(curve);
            }
        }
        crate::animation::workspace::set_window_config(Some(windows));
    } else {
        // Hyprland animates windows by default, with no `animation =` line. Without a window config the thumbnail
        // overlay stayed off and every re-tile fell back to resizing the real windows each frame: apps showed gray
        // and stretched while they caught up (minimize, close, new window). The overlay places them once instead.
        let windows = crate::animation::workspace::window_config().unwrap_or_default();
        crate::animation::workspace::set_window_config(Some(windows));
    }
    // border: a focus fade (Hyprland's "border" leaf); 200 ms when animations are on and nothing is said
    let fade = match leaf(conf, &["border", "global"]) {
        Some(a) if a.enabled => duration(a.speed).min(600),
        Some(_) => 0,
        None => if conf.get_bool("animations:enabled").unwrap_or(true) { 200 } else { 0 },
    };
    crate::border_manager::border::BORDER_FADE_MS.store(fade, Ordering::Relaxed);

    // gradient border: col.active_border = rgba(33ccffee) rgba(00ff99ee) 45deg (2+ colours)
    if let Some(value) = conf.get("general:col.active_border") {
        let (colours, angle) = parse_gradient(value);
        *crate::border_manager::border::GRADIENT.lock().unwrap() =
            if colours.len() >= 2 { Some(crate::border_manager::border::Gradient { colours, angle }) } else { None };
    }
    // spinning gradient: animation = borderangle, 1, <speed>, <curve>, loop  ("loop reverse" turns the other way)
    let spin = match leaf(conf, &["borderangle"]) {
        Some(a) if a.enabled && a.style.contains("loop") => (duration(a.speed).max(500), a.style.contains("reverse")),
        _ => (0, false),
    };
    // Aether's picker writes aether:border_spin_ms (ms per turn, 0 = still); it wins over borderangle when set
    let spin = match conf.get_i64("aether:border_spin_ms") {
        Some(ms) if ms > 0 => (ms.max(500) as u64, spin.1),
        Some(_) => (0, false),
        None => spin,
    };
    crate::border_manager::border::SPIN_MS.store(spin.0, Ordering::Relaxed);
    crate::border_manager::border::SPIN_REVERSE.store(spin.1, Ordering::Relaxed);
    if let Some(fps) = conf.get_i64("aether:border_fps") {
        crate::border_manager::border::SPIN_FPS.store(fps.clamp(5, 120) as u64, Ordering::Relaxed);
    }
    if spin.0 > 0 {
        crate::border_manager::border::ensure_spin_ticker();
    }

    // ---- rounding: Windows 11 corners (0 square, 1-6 small round, more = round) ----
    if let Some(rounding) = conf.get_i64("decoration:rounding") {
        let preference = match rounding {
            0 => 1,
            1..=6 => 3,
            _ => 2,
        };
        CORNERS.store(preference, Ordering::SeqCst);
        let _ = crate::process_command::run_command(wm, SocketMessage::BorderStyle(if rounding == 0 { BorderStyle::Square } else { BorderStyle::Rounded }));
        let windows: Vec<isize> = {
            let wm = wm.lock();
            wm.monitors()
                .iter()
                .flat_map(|m| m.workspaces().iter())
                .flat_map(|w| w.containers().iter().flat_map(|c| c.windows().iter().map(|x| x.hwnd)).chain(w.floating_windows().iter().map(|x| x.hwnd)))
                .collect()
        };
        for hwnd in windows {
            let _ = WindowsApi::set_corner_preference(hwnd, preference);
        }
    }

    // ---- opacity: inactive windows through komorebi's transparency ----
    if let Some(inactive) = conf.get_f64("decoration:inactive_opacity") {
        let alpha = (inactive.clamp(0.0, 1.0) * 255.0).round() as u8;
        let _ = crate::process_command::run_command(wm, SocketMessage::Transparency(alpha < 255));
        if alpha < 255 {
            let _ = crate::process_command::run_command(wm, SocketMessage::TransparencyAlpha(alpha));
        }
    }
    if conf.get_f64("decoration:active_opacity").is_some_and(|v| v < 1.0) {
        tracing::info!("hypr: decoration:active_opacity below 1 is not done on Windows (the focused window stays opaque)");
    }
}

/// a window opened: its corners like the others
pub fn window_opened(hwnd: isize) {
    let preference = CORNERS.load(Ordering::SeqCst);
    if preference >= 0 {
        let _ = WindowsApi::set_corner_preference(hwnd, preference);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn animations_map() {
        let conf = HyprConf::parse_str(
            "animations {\n enabled = yes\n bezier = myBezier, 0.05, 0.9, 0.1, 1.05\n animation = windows, 1, 7, myBezier\n animation = workspaces, 1, 6, default, slidevert\n}\n",
            std::path::Path::new("."),
        );
        let ws = leaf(&conf, &["workspaces", "global"]).unwrap();
        assert_eq!(duration(ws.speed), 600);
        assert!(matches!(workspace_kind(&ws.style), Some(WorkspaceAnimationKind::SlideVertical)));
        assert_eq!(bezier(&conf, "myBezier"), Some([0.05f32 as f64, 0.9f32 as f64, 0.1f32 as f64, 1.05f32 as f64]));
        assert_eq!(bezier(&conf, "default"), Some([0.0, 0.75, 0.15, 1.0]));
        assert!(leaf(&conf, &["windowsMove", "windows"]).is_some_and(|a| a.speed == 7.0));
        assert!(workspace_kind("popin 80%").is_none());
    }
}

/// Hyprland colours ("rgba(rrggbbaa)", "rgb(rrggbb)", "0xaarrggbb") and an optional "NNdeg" -> (r, g, b, a) list + angle
pub fn parse_gradient(value: &str) -> (Vec<(f32, f32, f32, f32)>, f32) {
    let mut colours = Vec::new();
    let mut angle = 0.0;
    for token in value.split_whitespace() {
        let f = |hex: &str, i: usize| u8::from_str_radix(hex.get(i..i + 2).unwrap_or("ff"), 16).unwrap_or(255) as f32 / 255.0;
        if let Some(deg) = token.strip_suffix("deg") {
            angle = deg.parse().unwrap_or(0.0);
        } else if let Some(hex) = token.strip_prefix("rgba(").or_else(|| token.strip_prefix("rgb(")) {
            let hex = hex.trim_end_matches(')');
            if hex.len() >= 6 {
                colours.push((f(hex, 0), f(hex, 2), f(hex, 4), if hex.len() >= 8 { f(hex, 6) } else { 1.0 }));
            }
        } else if let Some(hex) = token.strip_prefix("0x")
            && hex.len() == 8
        {
            colours.push((f(hex, 2), f(hex, 4), f(hex, 6), f(hex, 0)));
        }
    }
    (colours, angle)
}
