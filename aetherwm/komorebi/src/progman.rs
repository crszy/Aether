//! Aether: a stand-in for Explorer's desktop window, so Wallpaper Engine (and anything else that draws on the
//! desktop) works while Aether is the shell and Explorer isn't running.
//!
//! Wallpaper Engine finds the window of class "Progman", sends it 0x052C ("make a WorkerW") and draws its
//! wallpaper into the WorkerW that appears. On Windows 11 24H2+ that WorkerW is a child of Progman, under the
//! icons (SHELLDLL_DefView). We make the same: a Progman at the very bottom of the z-order covering every screen,
//! a hidden SHELLDLL_DefView child (hidden so Aether's own desktop layer doesn't treat it as the icons), and the
//! WorkerW child on 0x052C. Nothing is made when a real Progman exists (Explorer is the shell).
//! OFF by default: with it on, komorebi's virtual-desktop COM code panics ("Class not registered", com/mod.rs) because
//! a Progman exists but Explorer's ImmersiveShell does not. `aether { desktop_window = true }` turns it on - unsafe until
//! com/mod.rs handles that error.

use std::sync::Once;
use windows::Win32::Foundation::HWND;
use windows::Win32::Foundation::LPARAM;
use windows::Win32::Foundation::LRESULT;
use windows::Win32::Foundation::WPARAM;
use windows::Win32::UI::WindowsAndMessaging::CreateWindowExW;
use windows::Win32::UI::WindowsAndMessaging::DefWindowProcW;
use windows::Win32::UI::WindowsAndMessaging::DispatchMessageW;
use windows::Win32::UI::WindowsAndMessaging::FindWindowExW;
use windows::Win32::UI::WindowsAndMessaging::FindWindowW;
use windows::Win32::UI::WindowsAndMessaging::GetMessageW;
use windows::Win32::UI::WindowsAndMessaging::GetSystemMetrics;
use windows::Win32::UI::WindowsAndMessaging::SetWindowPos;
use windows::Win32::UI::WindowsAndMessaging::TranslateMessage;
use windows::Win32::UI::WindowsAndMessaging::HWND_BOTTOM;
use windows::Win32::UI::WindowsAndMessaging::MA_NOACTIVATE;
use windows::Win32::UI::WindowsAndMessaging::MSG;
use windows::Win32::UI::WindowsAndMessaging::SM_CXVIRTUALSCREEN;
use windows::Win32::UI::WindowsAndMessaging::SM_CYVIRTUALSCREEN;
use windows::Win32::UI::WindowsAndMessaging::SM_XVIRTUALSCREEN;
use windows::Win32::UI::WindowsAndMessaging::SM_YVIRTUALSCREEN;
use windows::Win32::UI::WindowsAndMessaging::SWP_NOACTIVATE;
use windows::Win32::UI::WindowsAndMessaging::SWP_NOZORDER;
use windows::Win32::UI::WindowsAndMessaging::WINDOWPOS;
use windows::Win32::UI::WindowsAndMessaging::WM_DISPLAYCHANGE;
use windows::Win32::UI::WindowsAndMessaging::WM_MOUSEACTIVATE;
use windows::Win32::UI::WindowsAndMessaging::WM_WINDOWPOSCHANGING;
use windows::Win32::UI::WindowsAndMessaging::WNDCLASSW;
use windows::Win32::UI::WindowsAndMessaging::WS_CHILD;
use windows::Win32::UI::WindowsAndMessaging::WS_CLIPCHILDREN;
use windows::Win32::UI::WindowsAndMessaging::WS_CLIPSIBLINGS;
use windows::Win32::UI::WindowsAndMessaging::WS_EX_NOACTIVATE;
use windows::Win32::UI::WindowsAndMessaging::WS_EX_TOOLWINDOW;
use windows::Win32::UI::WindowsAndMessaging::WS_POPUP;
use windows::Win32::UI::WindowsAndMessaging::WS_VISIBLE;
use windows::core::w;
use windows::core::PCWSTR;

use crate::windows_api::WindowsApi;

const SPAWN_WORKERW: u32 = 0x052C;
static START: Once = Once::new();

fn screen() -> (i32, i32, i32, i32) {
    unsafe {
        (
            GetSystemMetrics(SM_XVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_CXVIRTUALSCREEN),
            GetSystemMetrics(SM_CYVIRTUALSCREEN),
        )
    }
}

fn fit_children(progman: HWND) {
    let (_, _, w, h) = screen();
    unsafe {
        if let Ok(worker) = FindWindowExW(Some(progman), None, w!("WorkerW"), PCWSTR::null()) {
            let _ = SetWindowPos(worker, None, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

extern "system" fn progman_proc(hwnd: HWND, message: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    match message {
        SPAWN_WORKERW => {
            unsafe {
                if FindWindowExW(Some(hwnd), None, w!("WorkerW"), PCWSTR::null()).is_err() {
                    let (_, _, w, h) = screen();
                    let h_module = WindowsApi::module_handle_w().ok();
                    let _ = CreateWindowExW(
                        Default::default(),
                        w!("WorkerW"),
                        w!(""),
                        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                        0,
                        0,
                        w,
                        h,
                        Some(hwnd),
                        None,
                        h_module.map(|m| m.into()),
                        None,
                    );
                    tracing::info!("progman: made the desktop WorkerW (asked by another program)");
                }
            }
            LRESULT(0)
        }
        // always the bottom window, never activated
        WM_WINDOWPOSCHANGING => {
            if lparam.0 != 0 {
                let pos = unsafe { &mut *(lparam.0 as *mut WINDOWPOS) };
                pos.hwndInsertAfter = HWND_BOTTOM;
            }
            LRESULT(0)
        }
        WM_MOUSEACTIVATE => LRESULT(MA_NOACTIVATE as isize),
        WM_DISPLAYCHANGE => {
            let (x, y, w, h) = screen();
            unsafe {
                let _ = SetWindowPos(hwnd, Some(HWND_BOTTOM), x, y, w, h, SWP_NOACTIVATE);
            }
            fit_children(hwnd);
            LRESULT(0)
        }
        _ => unsafe { DefWindowProcW(hwnd, message, wparam, lparam) },
    }
}

extern "system" fn child_proc(hwnd: HWND, message: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    unsafe { DefWindowProcW(hwnd, message, wparam, lparam) }
}

/// Makes the stand-in desktop window once, on its own thread, unless Explorer's Progman is there
pub fn ensure() {
    START.call_once(|| {
        if unsafe { FindWindowW(w!("Progman"), PCWSTR::null()) }.is_ok() {
            tracing::info!("progman: Explorer's desktop window is there, not making one");
            return;
        }
        std::thread::spawn(|| unsafe {
            let Ok(h_module) = WindowsApi::module_handle_w() else { return };
            let progman_class = WNDCLASSW {
                hInstance: h_module.into(),
                lpszClassName: w!("Progman"),
                lpfnWndProc: Some(progman_proc),
                hbrBackground: WindowsApi::create_solid_brush(0),
                ..Default::default()
            };
            let _ = WindowsApi::register_class_w(&progman_class);
            for class in [w!("WorkerW"), w!("SHELLDLL_DefView")] {
                let c = WNDCLASSW {
                    hInstance: h_module.into(),
                    lpszClassName: class,
                    lpfnWndProc: Some(child_proc),
                    hbrBackground: WindowsApi::create_solid_brush(0),
                    ..Default::default()
                };
                let _ = WindowsApi::register_class_w(&c);
            }
            let (x, y, w, h) = screen();
            let Ok(progman) = CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                w!("Progman"),
                w!("Program Manager"),
                WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
                x,
                y,
                w,
                h,
                None,
                None,
                Some(h_module.into()),
                None,
            ) else {
                tracing::warn!("progman: could not make the desktop window");
                return;
            };
            // hidden: Wallpaper Engine looks for it, Aether's desktop layer only uses visible icons
            let _ = CreateWindowExW(
                Default::default(),
                w!("SHELLDLL_DefView"),
                w!(""),
                WS_CHILD,
                0,
                0,
                0,
                0,
                Some(progman),
                None,
                Some(h_module.into()),
                None,
            );
            let _ = SetWindowPos(progman, Some(HWND_BOTTOM), x, y, w, h, SWP_NOACTIVATE);
            tracing::info!("progman: made a desktop window for Wallpaper Engine");
            let mut msg = MSG::default();
            while GetMessageW(&mut msg, None, 0, 0).as_bool() {
                let _ = TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        });
    });
}
