//! Hyprland keybinds, handled by the window manager itself (no whkd, no process per key press).
//!
//! One thread owns a low-level keyboard hook and a low-level mouse hook. The hooks only match binds and
//! queue work; a worker thread runs the dispatchers (the hooks must return quickly or Windows drops them).
//!
//! - `bind` fires on key down, `bindr` on release, `binde` repeats while held, `bindn` lets the key through,
//!   `bindm` drags the window under the cursor (movewindow / resizewindow) while the button is held.
//! - Submaps: only binds of the active submap match (`submap = name` / `submap, reset`).
//! - A consumed combo with SUPER or ALT injects an unassigned key (0xE8) so the Start menu / menu bar does not
//!   open on release - and Aether's shell, which opens its launcher on a lone SUPER tap, sees a key press too.

use std::collections::HashSet;
use std::sync::Arc;
use std::sync::OnceLock;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::AtomicU8;
use std::sync::atomic::Ordering;

use crossbeam_channel::Sender;
use parking_lot::Mutex;
use parking_lot::RwLock;
use windows::Win32::Foundation::LPARAM;
use windows::Win32::Foundation::LRESULT;
use windows::Win32::Foundation::POINT;
use windows::Win32::Foundation::WPARAM;
use windows::Win32::UI::Input::KeyboardAndMouse::INPUT;
use windows::Win32::UI::Input::KeyboardAndMouse::INPUT_0;
use windows::Win32::UI::Input::KeyboardAndMouse::INPUT_KEYBOARD;
use windows::Win32::UI::Input::KeyboardAndMouse::KEYBD_EVENT_FLAGS;
use windows::Win32::UI::Input::KeyboardAndMouse::KEYBDINPUT;
use windows::Win32::UI::Input::KeyboardAndMouse::KEYEVENTF_KEYUP;
use windows::Win32::UI::Input::KeyboardAndMouse::MAPVK_VSC_TO_VK;
use windows::Win32::UI::Input::KeyboardAndMouse::MapVirtualKeyW;
use windows::Win32::UI::Input::KeyboardAndMouse::SendInput;
use windows::Win32::UI::Input::KeyboardAndMouse::VIRTUAL_KEY;
use windows::Win32::UI::WindowsAndMessaging::CallNextHookEx;
use windows::Win32::UI::WindowsAndMessaging::DispatchMessageW;
use windows::Win32::UI::WindowsAndMessaging::GA_ROOT;
use windows::Win32::UI::WindowsAndMessaging::GetAncestor;
use windows::Win32::UI::WindowsAndMessaging::GetMessageW;
use windows::Win32::UI::WindowsAndMessaging::HC_ACTION;
use windows::Win32::UI::WindowsAndMessaging::KBDLLHOOKSTRUCT;
use windows::Win32::UI::WindowsAndMessaging::LLKHF_INJECTED;
use windows::Win32::UI::WindowsAndMessaging::LLKHF_UP;
use windows::Win32::UI::WindowsAndMessaging::MSG;
use windows::Win32::UI::WindowsAndMessaging::MSLLHOOKSTRUCT;
use windows::Win32::UI::WindowsAndMessaging::SetWindowsHookExW;
use windows::Win32::UI::WindowsAndMessaging::TranslateMessage;
use windows::Win32::UI::WindowsAndMessaging::WH_KEYBOARD_LL;
use windows::Win32::UI::WindowsAndMessaging::WH_MOUSE_LL;
use windows::Win32::UI::WindowsAndMessaging::WM_LBUTTONDOWN;
use windows::Win32::UI::WindowsAndMessaging::WM_LBUTTONUP;
use windows::Win32::UI::WindowsAndMessaging::WM_MBUTTONDOWN;
use windows::Win32::UI::WindowsAndMessaging::WM_MBUTTONUP;
use windows::Win32::UI::WindowsAndMessaging::WM_MOUSEMOVE;
use windows::Win32::UI::WindowsAndMessaging::WM_MOUSEWHEEL;
use windows::Win32::UI::WindowsAndMessaging::WM_RBUTTONDOWN;
use windows::Win32::UI::WindowsAndMessaging::WM_RBUTTONUP;
use windows::Win32::UI::WindowsAndMessaging::WindowFromPoint;

use super::conf::Bind;
use super::conf::Mods;
use super::dispatch::Action;
use super::dispatch::MouseDrag;
use crate::Window;
use crate::core::Rect;
use crate::window_manager::WindowManager;
use crate::window_manager_event::WindowManagerEvent;
use crate::windows_api::WindowsApi;
use crate::winevent::WinEvent;
use crate::winevent_listener;

/// dwExtraInfo of the keys we inject ourselves ("HYPR"); the hook lets them pass untouched
const MARKER: usize = 0x4859_5052;
const MASK_KEY: u16 = 0xE8;

const SHIFT: u8 = 1;
const CTRL: u8 = 2;
const ALT: u8 = 4;
const SUPER: u8 = 8;

/// A bind ready for matching.
#[derive(Debug, Clone)]
struct Compiled {
    trigger: Trigger,
    mods: u8,
    bind: Bind,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Trigger {
    Key(u32),
    /// 1 left, 2 right, 3 middle
    Button(u8),
    /// +1 up, -1 down
    Wheel(i8),
}

enum Job {
    Run(String, String),
    DragEnd(isize),
}

struct Drag {
    kind: MouseDrag,
    button: u8,
    hwnd: isize,
    start: POINT,
    rect: Rect,
}

static TABLE: RwLock<Vec<Compiled>> = RwLock::new(Vec::new());
static SUBMAP: RwLock<Option<String>> = RwLock::new(None);
static MODS: AtomicU8 = AtomicU8::new(0);
static JOBS: OnceLock<Sender<Job>> = OnceLock::new();
static DRAG: Mutex<Option<Drag>> = Mutex::new(None);
static STARTED: AtomicBool = AtomicBool::new(false);
/// keys held down (auto-repeat detection, release binds)
static HELD: Mutex<Option<HashSet<u32>>> = Mutex::new(None);
/// a non-modifier key went down since the last modifier press (for `bindr = SUPER, SUPER_L, ...`)
static OTHER_KEY: AtomicBool = AtomicBool::new(false);
/// buttons whose press we consumed: their release is consumed too
static EATEN_BUTTONS: AtomicU8 = AtomicU8::new(0);

fn mod_bits(m: &Mods) -> u8 {
    (if m.shift { SHIFT } else { 0 }) | (if m.ctrl { CTRL } else { 0 }) | (if m.alt { ALT } else { 0 }) | (if m.sup { SUPER } else { 0 })
}

fn modifier_bit(vk: u32) -> u8 {
    match vk {
        0x10 | 0xA0 | 0xA1 => SHIFT,
        0x11 | 0xA2 | 0xA3 => CTRL,
        0x12 | 0xA4 | 0xA5 => ALT,
        0x5B | 0x5C => SUPER,
        _ => 0,
    }
}

/// Hyprland key name (xkb keysym) -> Windows virtual key
pub fn vk_for(name: &str) -> Option<u32> {
    let n = name.trim();
    if let Some(code) = n.strip_prefix("code:") {
        // xkb keycode = evdev code + 8; evdev codes are PC scancodes for the main block
        let code: u32 = code.trim().parse().ok()?;
        let vk = unsafe { MapVirtualKeyW(code.checked_sub(8)?, MAPVK_VSC_TO_VK) };
        return (vk != 0).then_some(vk);
    }
    if n.chars().count() == 1 {
        let c = n.chars().next()?.to_ascii_uppercase();
        if c.is_ascii_alphanumeric() {
            return Some(c as u32);
        }
    }
    let lower = n.to_ascii_lowercase();
    if let Some(num) = lower.strip_prefix('f').and_then(|d| d.parse::<u32>().ok())
        && (1..=24).contains(&num)
    {
        return Some(0x6F + num);
    }
    Some(match lower.as_str() {
        "return" | "enter" | "kp_enter" => 0x0D,
        "space" => 0x20,
        "tab" => 0x09,
        "escape" | "esc" => 0x1B,
        "backspace" => 0x08,
        "delete" => 0x2E,
        "insert" => 0x2D,
        "home" => 0x24,
        "end" => 0x23,
        "prior" | "page_up" | "pageup" => 0x21,
        "next" | "page_down" | "pagedown" => 0x22,
        "left" => 0x25,
        "up" => 0x26,
        "right" => 0x27,
        "down" => 0x28,
        "print" => 0x2C,
        "pause" => 0x13,
        "caps_lock" => 0x14,
        "comma" => 0xBC,
        "period" => 0xBE,
        "minus" => 0xBD,
        "equal" | "plus" => 0xBB,
        "slash" => 0xBF,
        "backslash" => 0xDC,
        "semicolon" => 0xBA,
        "apostrophe" => 0xDE,
        "grave" => 0xC0,
        "bracketleft" => 0xDB,
        "bracketright" => 0xDD,
        "super_l" => 0x5B,
        "super_r" => 0x5C,
        "alt_l" => 0xA4,
        "alt_r" => 0xA5,
        "control_l" => 0xA2,
        "control_r" => 0xA3,
        "shift_l" => 0xA0,
        "shift_r" => 0xA1,
        "xf86audioraisevolume" => 0xAF,
        "xf86audiolowervolume" => 0xAE,
        "xf86audiomute" => 0xAD,
        "xf86audioplay" | "xf86audiopause" => 0xB3,
        "xf86audionext" => 0xB0,
        "xf86audioprev" => 0xB1,
        "xf86audiostop" => 0xB2,
        "kp_0" => 0x60,
        "kp_1" => 0x61,
        "kp_2" => 0x62,
        "kp_3" => 0x63,
        "kp_4" => 0x64,
        "kp_5" => 0x65,
        "kp_6" => 0x66,
        "kp_7" => 0x67,
        "kp_8" => 0x68,
        "kp_9" => 0x69,
        _ => return None,
    })
}

fn trigger_for(key: &str) -> Option<Trigger> {
    match key.trim().to_ascii_lowercase().as_str() {
        "mouse:272" => Some(Trigger::Button(1)),
        "mouse:273" => Some(Trigger::Button(2)),
        "mouse:274" => Some(Trigger::Button(3)),
        "mouse_up" => Some(Trigger::Wheel(1)),
        "mouse_down" => Some(Trigger::Wheel(-1)),
        _ => vk_for(key).map(Trigger::Key),
    }
}

/// Windows reserves these SUPER combos below any hook (they can never be bound): SUPER+L locks the PC.
pub const RESERVED_SUPER_KEYS: &[&str] = &["L"];

/// `alt_too`: every SUPER bind also works with ALT instead (unless that ALT combo is bound already), so ALT can be
/// the main modifier while SUPER keeps working - and SUPER+L, which Windows keeps for locking, gets a usable ALT+L.
pub fn with_alt_variants(binds: &[Bind]) -> Vec<Bind> {
    let mut out: Vec<Bind> = binds.to_vec();
    for bind in binds {
        if !bind.mods.sup || bind.mods.alt {
            continue;
        }
        let mut alt = bind.clone();
        alt.mods.sup = false;
        alt.mods.alt = true;
        let taken = binds.iter().any(|b| {
            b.key.eq_ignore_ascii_case(&alt.key) && b.mods == alt.mods && b.submap == alt.submap && b.flags.release == alt.flags.release
        });
        if !taken {
            out.push(alt);
        }
    }
    out
}

/// Replaces the bind table (config load / reload). Returns the keys that could not be understood.
pub fn set_binds(binds: &[Bind]) -> Vec<String> {
    let mut table = Vec::new();
    let mut unknown = Vec::new();
    for bind in binds {
        if bind.mods.sup && RESERVED_SUPER_KEYS.iter().any(|k| bind.key.eq_ignore_ascii_case(k)) {
            tracing::warn!("hypr: SUPER+{} is kept by Windows (it locks the PC) and cannot be bound - use the ALT version (--hyprland-alt)", bind.key);
        }
        match trigger_for(&bind.key) {
            Some(trigger) => table.push(Compiled { trigger, mods: mod_bits(&bind.mods), bind: bind.clone() }),
            None => unknown.push(bind.key.clone()),
        }
    }
    *TABLE.write() = table;
    *SUBMAP.write() = None;
    unknown
}

/// the binds in use now (keyword bind = ... adds to them)
pub fn current_binds() -> Vec<Bind> {
    TABLE.read().iter().map(|c| c.bind.clone()).collect()
}

pub fn current_submap() -> Option<String> {
    SUBMAP.read().clone()
}

/// Starts the hook thread and the worker (once). Binds come from `set_binds`.
pub fn start(wm: Arc<Mutex<WindowManager>>) {
    if STARTED.swap(true, Ordering::SeqCst) {
        return;
    }
    let (tx, rx) = crossbeam_channel::unbounded::<Job>();
    let _ = JOBS.set(tx);
    *HELD.lock() = Some(HashSet::new());

    std::thread::Builder::new()
        .name("hypr-binds-worker".into())
        .spawn(move || {
            for job in rx {
                match job {
                    Job::Run(dispatcher, arg) => run(&wm, &dispatcher, &arg),
                    Job::DragEnd(hwnd) => {
                        // komorebi re-tiles / swaps / resizes exactly as after a title-bar drag
                        let window = Window::from(hwnd);
                        let _ = winevent_listener::event_tx().send(WindowManagerEvent::MoveResizeEnd(WinEvent::SystemMoveSizeEnd, window));
                    }
                }
            }
        })
        .expect("could not start the keybind worker");

    std::thread::Builder::new()
        .name("hypr-binds-hook".into())
        .spawn(|| unsafe {
            let keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, Some(keyboard_proc), None, 0);
            let mouse = SetWindowsHookExW(WH_MOUSE_LL, Some(mouse_proc), None, 0);
            if keyboard.is_err() || mouse.is_err() {
                tracing::error!("hypr: could not install the keyboard/mouse hooks: {:?} {:?}", keyboard.err(), mouse.err());
                return;
            }
            tracing::info!("hypr: keybinds active");
            let mut msg = MSG::default();
            while GetMessageW(&mut msg, None, 0, 0).as_bool() {
                let _ = TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        })
        .expect("could not start the keybind hook thread");
}

/// Runs one dispatcher (also used by the IPC's `dispatch`).
pub fn run(wm: &Arc<Mutex<WindowManager>>, dispatcher: &str, arg: &str) {
    // the infinite canvas (milestone 7): canvas = go there / back, movetocanvas = push the active window onto it
    match dispatcher {
        "canvas" => return super::special::canvas_toggle(wm),
        "movetocanvas" => return super::special::canvas_push(wm),
        "aether" => return super::ipc::aether_event(arg), // eg. aether, wallpaper next (the shell listens)
        "workspace" => super::special::canvas_leave(wm, false),
        _ => {}
    }
    // dwindle workspaces: togglesplit / resizeactive act on the split itself (exact pixels, one split)
    let dwindle_op = match dispatcher {
        "layoutmsg" if arg.trim() == "togglesplit" => Some(("togglesplit", "")),
        "togglesplit" | "resizeactive" => Some((dispatcher, arg)),
        _ => None,
    };
    if let Some((d, a)) = dwindle_op
        && super::dwindle_action(wm, d, a)
    {
        return;
    }
    // master workspaces: layoutmsg mfact / orientation...
    if dispatcher == "layoutmsg" && super::master_action(wm, arg) {
        return;
    }
    // fullscreen 0 (and fullscreenstate): the whole monitor; fullscreen 1 stays maximize-with-gaps (monocle)
    if (dispatcher == "fullscreen" && matches!(arg.trim(), "" | "0")) || dispatcher == "fullscreenstate" {
        super::toggle_true_fullscreen(wm);
        return;
    }
    let action = super::dispatch::translate(dispatcher, arg);
    if dispatcher.starts_with("movetoworkspace") && matches!(action, Action::Wm(_))
        && let Ok(hwnd) = WindowsApi::foreground_window()
        && super::special::release(hwnd)
    {
        // komorebi manages it again (asynchronously); give it a moment before moving it
        std::thread::sleep(std::time::Duration::from_millis(150));
    }
    match action {
        Action::Wm(messages) => {
            for message in messages {
                if let Err(error) = crate::process_command::run_command(wm, message) {
                    tracing::warn!("hypr: {dispatcher} {arg}: {error}");
                }
            }
        }
        Action::Exec(command) => exec(&command),
        Action::Submap(name) => {
            tracing::info!("hypr: submap {:?}", name);
            super::ipc::submap_changed(name.as_deref());
            *SUBMAP.write() = name;
        }
        Action::ToggleSpecial(name) => super::special::toggle(wm, &name),
        Action::MoveToSpecial { name, silent } => super::special::move_active(wm, &name, silent),
        Action::Drag(_) => {} // handled by the mouse hook
        Action::Nothing => {}
        Action::Unsupported(what) => tracing::info!("hypr: not supported yet: {what}"),
    }
}

/// `exec`: through the Windows shell, detached, without a console window
pub fn exec(command: &str) {
    use std::os::windows::process::CommandExt;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    let command = command.trim();
    if command.is_empty() {
        return;
    }
    let result = std::process::Command::new("cmd.exe")
        .raw_arg(format!("/D /C start \"\" {command}"))
        .creation_flags(CREATE_NO_WINDOW)
        .spawn();
    if let Err(error) = result {
        tracing::warn!("hypr: exec {command}: {error}");
    }
}

fn queue(bind: &Bind) {
    if let Some(tx) = JOBS.get() {
        let _ = tx.send(Job::Run(bind.dispatcher.clone(), bind.arg.clone()));
    }
}

fn inject_mask() {
    let key = |up: bool| INPUT {
        r#type: INPUT_KEYBOARD,
        Anonymous: INPUT_0 {
            ki: KEYBDINPUT {
                wVk: VIRTUAL_KEY(MASK_KEY),
                wScan: 0,
                dwFlags: if up { KEYEVENTF_KEYUP } else { KEYBD_EVENT_FLAGS(0) },
                time: 0,
                dwExtraInfo: MARKER,
            },
        },
    };
    unsafe {
        SendInput(&[key(false), key(true)], std::mem::size_of::<INPUT>() as i32);
    }
}

/// the binds of the active submap with this trigger whose mods match (`bindi` ignores mods)
fn matching(trigger: Trigger, mods: u8, release: bool) -> Vec<Bind> {
    let submap = SUBMAP.read().clone();
    TABLE
        .read()
        .iter()
        .filter(|c| c.trigger == trigger && c.bind.submap == submap && c.bind.flags.release == release)
        .filter(|c| {
            if c.bind.flags.ignore_mods {
                return true;
            }
            // a modifier key bound to itself (`bindr = SUPER, SUPER_L`): its own bit is not required twice
            let own = match trigger {
                Trigger::Key(vk) => modifier_bit(vk),
                _ => 0,
            };
            c.mods & !own == mods & !own
        })
        .map(|c| c.bind.clone())
        .collect()
}

unsafe extern "system" fn keyboard_proc(code: i32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    if code as u32 != HC_ACTION {
        return unsafe { CallNextHookEx(None, code, wparam, lparam) };
    }
    let info = unsafe { &*(lparam.0 as *const KBDLLHOOKSTRUCT) };
    let injected = info.flags.0 & LLKHF_INJECTED.0 != 0;
    if injected && info.dwExtraInfo == MARKER {
        return unsafe { CallNextHookEx(None, code, wparam, lparam) };
    }
    let up = info.flags.0 & LLKHF_UP.0 != 0;
    let vk = info.vkCode;

    let bit = modifier_bit(vk);
    if bit != 0 {
        if up {
            // release binds on a lone modifier (`bindr = SUPER, SUPER_L, exec, ...`)
            let mods = MODS.load(Ordering::SeqCst);
            let lone = !OTHER_KEY.load(Ordering::SeqCst);
            MODS.fetch_and(!bit, Ordering::SeqCst);
            if lone {
                let binds = matching(Trigger::Key(vk), mods, true);
                for bind in &binds {
                    queue(bind);
                }
            }
        } else {
            if MODS.load(Ordering::SeqCst) & bit == 0 {
                OTHER_KEY.store(false, Ordering::SeqCst);
            }
            MODS.fetch_or(bit, Ordering::SeqCst);
        }
        return unsafe { CallNextHookEx(None, code, wparam, lparam) };
    }

    let mods = MODS.load(Ordering::SeqCst);
    let mut held_guard = HELD.lock();
    let held = held_guard.get_or_insert_with(HashSet::new);

    if up {
        held.remove(&vk);
        drop(held_guard);
        let binds = matching(Trigger::Key(vk), mods, true);
        if !binds.is_empty() {
            for bind in &binds {
                queue(bind);
            }
            if binds.iter().any(|b| !b.flags.non_consuming) {
                return LRESULT(1);
            }
        }
        // the key's press was consumed by a bind: eat its release as well
        if TABLE.read().iter().any(|c| c.trigger == Trigger::Key(vk) && !c.bind.flags.non_consuming && !c.bind.flags.release && c.mods == mods) {
            return LRESULT(1);
        }
        return unsafe { CallNextHookEx(None, code, wparam, lparam) };
    }

    OTHER_KEY.store(true, Ordering::SeqCst);
    let repeat = !held.insert(vk);
    drop(held_guard);

    let binds = matching(Trigger::Key(vk), mods, false);
    if binds.is_empty() {
        return unsafe { CallNextHookEx(None, code, wparam, lparam) };
    }
    for bind in &binds {
        if !repeat || bind.flags.repeat {
            queue(bind);
        }
    }
    if binds.iter().all(|b| b.flags.non_consuming) {
        return unsafe { CallNextHookEx(None, code, wparam, lparam) };
    }
    if !repeat && mods & (SUPER | ALT) != 0 {
        inject_mask();
    }
    LRESULT(1)
}

/// a window bindm may drag: titled, not a tool/no-activate window (the shell's wallpaper, bars, the desktop are not)
fn draggable(hwnd: isize) -> bool {
    const WS_EX_TOOLWINDOW: isize = 0x80;
    const WS_EX_NOACTIVATE: isize = 0x0800_0000;
    const WS_EX_APPWINDOW: isize = 0x40000;
    let ex = WindowsApi::gwl_ex_style(hwnd).unwrap_or(0) as isize;
    if ex & WS_EX_NOACTIVATE != 0 || (ex & WS_EX_TOOLWINDOW != 0 && ex & WS_EX_APPWINDOW == 0) {
        return false;
    }
    let class = WindowsApi::real_window_class_w(hwnd).unwrap_or_default();
    !matches!(class.as_str(), "Progman" | "WorkerW" | "Shell_TrayWnd" | "Shell_SecondaryTrayWnd")
        && !WindowsApi::window_text_w(hwnd).unwrap_or_default().is_empty()
}

fn root_window_at(pt: POINT) -> isize {
    unsafe {
        let hwnd = WindowFromPoint(pt);
        let root = GetAncestor(hwnd, GA_ROOT);
        if root.0.is_null() { hwnd.0 as isize } else { root.0 as isize }
    }
}

unsafe extern "system" fn mouse_proc(code: i32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    if code as u32 != HC_ACTION {
        return unsafe { CallNextHookEx(None, code, wparam, lparam) };
    }
    let info = unsafe { &*(lparam.0 as *const MSLLHOOKSTRUCT) };
    let message = wparam.0 as u32;
    let mods = MODS.load(Ordering::SeqCst);

    let button = match message {
        WM_LBUTTONDOWN | WM_LBUTTONUP => 1,
        WM_RBUTTONDOWN | WM_RBUTTONUP => 2,
        WM_MBUTTONDOWN | WM_MBUTTONUP => 3,
        _ => 0,
    };
    let down = matches!(message, WM_LBUTTONDOWN | WM_RBUTTONDOWN | WM_MBUTTONDOWN);

    match message {
        WM_MOUSEMOVE => {
            let drag = DRAG.lock();
            if let Some(drag) = drag.as_ref() {
                let dx = info.pt.x - drag.start.x;
                let dy = info.pt.y - drag.start.y;
                let rect = match drag.kind {
                    MouseDrag::Move => Rect { left: drag.rect.left + dx, top: drag.rect.top + dy, ..drag.rect },
                    MouseDrag::Resize => Rect { right: (drag.rect.right + dx).max(120), bottom: (drag.rect.bottom + dy).max(80), ..drag.rect },
                };
                let _ = WindowsApi::position_window_without_waiting(drag.hwnd, &rect, false);
            }
        }
        _ if button != 0 && down => {
            let binds = matching(Trigger::Button(button), mods, false);
            if let Some(bind) = binds.first() {
                OTHER_KEY.store(true, Ordering::SeqCst);
                if bind.flags.mouse {
                    let kind = if bind.dispatcher == "resizewindow" { MouseDrag::Resize } else { MouseDrag::Move };
                    let hwnd = root_window_at(info.pt);
                    if !draggable(hwnd) {
                        // not an app window: let the click through untouched
                        return unsafe { CallNextHookEx(None, code, wparam, lparam) };
                    }
                    if let Ok(rect) = WindowsApi::window_rect(hwnd) {
                        *DRAG.lock() = Some(Drag { kind, button, hwnd, start: info.pt, rect });
                    }
                } else {
                    queue(bind);
                }
                EATEN_BUTTONS.fetch_or(1 << button, Ordering::SeqCst);
                if mods & (SUPER | ALT) != 0 {
                    inject_mask();
                }
                return LRESULT(1);
            }
        }
        _ if button != 0 => {
            if EATEN_BUTTONS.fetch_and(!(1 << button), Ordering::SeqCst) & (1 << button) != 0 {
                let mut drag = DRAG.lock();
                if drag.as_ref().is_some_and(|d| d.button == button) {
                    let hwnd = drag.take().map(|d| d.hwnd).unwrap_or_default();
                    if let Some(tx) = JOBS.get() {
                        let _ = tx.send(Job::DragEnd(hwnd));
                    }
                }
                return LRESULT(1);
            }
        }
        WM_MOUSEWHEEL => {
            let delta = (info.mouseData >> 16) as i16;
            let binds = matching(Trigger::Wheel(if delta > 0 { 1 } else { -1 }), mods, false);
            if !binds.is_empty() {
                for bind in &binds {
                    queue(bind);
                }
                return LRESULT(1);
            }
        }
        _ => {}
    }
    unsafe { CallNextHookEx(None, code, wparam, lparam) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn key_names() {
        assert_eq!(vk_for("Q"), Some('Q' as u32));
        assert_eq!(vk_for("q"), Some('Q' as u32));
        assert_eq!(vk_for("1"), Some('1' as u32));
        assert_eq!(vk_for("Return"), Some(0x0D));
        assert_eq!(vk_for("left"), Some(0x25));
        assert_eq!(vk_for("F12"), Some(0x7B));
        assert_eq!(vk_for("XF86AudioRaiseVolume"), Some(0xAF));
        assert_eq!(vk_for("SUPER_L"), Some(0x5B));
        assert_eq!(vk_for("nonsense_key"), None);
        assert_eq!(trigger_for("mouse:272"), Some(Trigger::Button(1)));
        assert_eq!(trigger_for("mouse_down"), Some(Trigger::Wheel(-1)));
    }

    #[test]
    fn alt_variants() {
        let conf = super::super::conf::HyprConf::parse_str(
            "bind = SUPER, Q, killactive
bind = SUPER, L, exec, a
bind = ALT, L, exec, mine
bindm = SUPER, mouse:272, movewindow
bind = SUPER ALT, X, exec, b
",
            std::path::Path::new("."),
        );
        let all = with_alt_variants(&conf.binds);
        let alt: Vec<String> = all.iter().filter(|b| b.mods.alt && !b.mods.sup).map(|b| format!("{}:{}", b.key, b.arg)).collect();
        // ALT+Q added, ALT+L kept as the user wrote it (not replaced), ALT drag added, SUPER+ALT+X untouched
        assert_eq!(alt, vec!["L:mine".to_string(), "Q:".to_string(), "mouse:272:".to_string()]);
    }

    #[test]
    fn the_users_config_compiles() {
        let conf = super::super::conf::HyprConf::parse_str(
            "$mainMod = SUPER\nbind = $mainMod, Return, exec, wt.exe\nbind = $mainMod CTRL, left, resizeactive, -40 0\nbindm = $mainMod, mouse:272, movewindow\nbind = $mainMod, S, togglespecialworkspace, magic\n",
            std::path::Path::new("."),
        );
        assert!(set_binds(&conf.binds).is_empty());
        let table = TABLE.read();
        assert_eq!(table.len(), 4);
        assert_eq!(table[1].mods, SUPER | CTRL);
        assert_eq!(table[2].trigger, Trigger::Button(1));
    }
}
