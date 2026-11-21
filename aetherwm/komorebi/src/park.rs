//! Aether: hiding windows without Explorer.
//!
//! komorebi's `Cloak` hiding goes through Explorer's immersive shell. A shell which replaces
//! Explorer (Caelestia) has none, so cloaked hiding would fail. Instead, windows of hidden
//! workspaces are parked far off-screen with their size kept. DWM keeps drawing parked windows,
//! so their live thumbnails still work for the workspace slide animation (a minimized window
//! has no thumbnail, which is why `Minimize` hiding slid in empty workspaces).
//!
//! Parked windows and their places are also written to `aether-parked.json` in komorebi's data
//! directory, so that if komorebi stops without restoring them they are brought back the next
//! time it starts.

use crate::DATA_DIR;

use lazy_static::lazy_static;
use parking_lot::Mutex;
use std::collections::HashMap;
use windows::Win32::Foundation::HWND;
use windows::Win32::Foundation::RECT;
use windows::Win32::UI::WindowsAndMessaging::GetWindowRect;
use windows::Win32::UI::WindowsAndMessaging::IsWindow;
use windows::Win32::UI::WindowsAndMessaging::SWP_NOACTIVATE;
use windows::Win32::UI::WindowsAndMessaging::SWP_NOSIZE;
use windows::Win32::UI::WindowsAndMessaging::SWP_NOZORDER;
use windows::Win32::UI::WindowsAndMessaging::SetWindowPos;

/// Far enough away from any monitor; not -32000, which Windows uses for minimized windows
const PARK_X: i32 = -24000;
const PARK_Y: i32 = -24000;

lazy_static! {
    /// window -> its top-left corner before it was parked
    static ref PARKED: Mutex<HashMap<isize, (i32, i32)>> = Mutex::new(HashMap::new());
}

fn file() -> std::path::PathBuf {
    DATA_DIR.join("aether-parked.json")
}

fn save(parked: &HashMap<isize, (i32, i32)>) {
    let list: Vec<(isize, i32, i32)> = parked.iter().map(|(h, (x, y))| (*h, *x, *y)).collect();
    if let Ok(json) = serde_json::to_string(&list) {
        let _ = std::fs::write(file(), json);
    }
}

fn move_to(hwnd: isize, x: i32, y: i32) {
    unsafe {
        let _ = SetWindowPos(
            HWND(hwnd as _),
            None,
            x,
            y,
            0,
            0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE,
        );
    }
}

pub fn is_parked(hwnd: isize) -> bool {
    PARKED.lock().contains_key(&hwnd)
}

pub fn park(hwnd: isize) {
    let mut parked = PARKED.lock();
    if !parked.contains_key(&hwnd) {
        let mut rect = RECT::default();
        if unsafe { GetWindowRect(HWND(hwnd as _), &mut rect) }.is_err() {
            return;
        }
        // already off-screen (eg. parked by a run that did not restore it): keep the old place
        if rect.left <= PARK_X / 2 && rect.top <= PARK_Y / 2 {
            return;
        }
        parked.insert(hwnd, (rect.left, rect.top));
        save(&parked);
    }
    move_to(hwnd, PARK_X, PARK_Y);
}

pub fn unpark(hwnd: isize) {
    let mut parked = PARKED.lock();
    if let Some((x, y)) = parked.remove(&hwnd) {
        save(&parked);
        drop(parked);
        move_to(hwnd, x, y);
    }
}

/// Brings back windows a previous run left parked (it crashed or was killed)
pub fn restore_left_over() {
    let Ok(json) = std::fs::read_to_string(file()) else {
        return;
    };
    let Ok(list) = serde_json::from_str::<Vec<(isize, i32, i32)>>(&json) else {
        return;
    };
    for (hwnd, x, y) in list {
        if !unsafe { IsWindow(Some(HWND(hwnd as _))) }.as_bool() {
            continue;
        }
        let mut rect = RECT::default();
        if unsafe { GetWindowRect(HWND(hwnd as _), &mut rect) }.is_ok()
            && rect.left <= PARK_X / 2
            && rect.top <= PARK_Y / 2
        {
            move_to(hwnd, x, y);
        }
    }
    let _ = std::fs::remove_file(file());
}

/// Where a parked window was before it was parked (its top-left corner)
pub fn original(hwnd: isize) -> Option<(i32, i32)> {
    PARKED.lock().get(&hwnd).copied()
}

/// Aether canvas: the place a parked window goes back to (it slid away before being parked)
pub fn set_original(hwnd: isize, x: i32, y: i32) {
    let mut parked = PARKED.lock();
    if let Some(place) = parked.get_mut(&hwnd) {
        *place = (x, y);
        save(&parked);
    }
}

/// Aether canvas: forget a parked window without moving it (the caller places it itself)
pub fn forget(hwnd: isize) {
    let mut parked = PARKED.lock();
    if parked.remove(&hwnd).is_some() {
        save(&parked);
    }
}
