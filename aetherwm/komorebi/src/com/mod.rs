// This code is largely taken verbatim from this repository: https://github.com/Ciantic/AltTabAccessor
// which the author Jari Pennanen (Ciantic) has kindly made available with the MIT license, available
// in full here: https://github.com/Ciantic/AltTabAccessor/blob/main/LICENSE.txt

mod interfaces;

use interfaces::CLSID_ImmersiveShell;
use interfaces::IApplicationViewCollection;
use interfaces::IServiceProvider;

use std::ffi::c_void;

use windows::Win32::Foundation::HWND;
use windows::Win32::System::Com::CLSCTX_ALL;
use windows::Win32::System::Com::COINIT_MULTITHREADED;
use windows::Win32::System::Com::CoCreateInstance;
use windows::Win32::System::Com::CoInitializeEx;
use windows::Win32::System::Com::CoUninitialize;
use windows_core::Interface;

struct ComInit();

impl ComInit {
    pub fn new() -> Self {
        unsafe {
            CoInitializeEx(None, COINIT_MULTITHREADED).unwrap();
        }
        Self()
    }
}

impl Drop for ComInit {
    fn drop(&mut self) {
        unsafe {
            CoUninitialize();
        }
    }
}

thread_local! {
    static COM_INIT: ComInit = ComInit::new();
}

#[allow(dead_code)]
fn get_iservice_provider() -> IServiceProvider {
    COM_INIT.with(|_| unsafe { CoCreateInstance(&CLSID_ImmersiveShell, None, CLSCTX_ALL).unwrap() })
}

fn get_iapplication_view_collection(provider: &IServiceProvider) -> IApplicationViewCollection {
    COM_INIT.with(|_| {
        let mut obj = std::ptr::null_mut::<c_void>();
        unsafe {
            provider
                .query_service(
                    &IApplicationViewCollection::IID,
                    &IApplicationViewCollection::IID,
                    &mut obj,
                )
                .unwrap();
        }

        assert!(!obj.is_null());

        unsafe { IApplicationViewCollection::from_raw(obj) }
    })
}

/// Aether: cloaking goes through Explorer's immersive shell. With a shell that replaces Explorer
/// (Caelestia) there is none, and asking for it would fail (or start Explorer), so callers park
/// windows off-screen instead. Explorer's desktop window (Progman) tells whether it runs.
pub fn cloak_available() -> bool {
    use windows::Win32::UI::WindowsAndMessaging::FindWindowW;
    use windows::core::w;
    // Aether: our own stand-in Progman (progman.rs, for Wallpaper Engine) doesn't count - only Explorer's
    match unsafe { FindWindowW(w!("Progman"), windows::core::PCWSTR::null()) } {
        Ok(h) => {
            let mut pid = 0u32;
            unsafe { windows::Win32::UI::WindowsAndMessaging::GetWindowThreadProcessId(h, Some(&mut pid)) };
            pid != std::process::id()
        }
        Err(_) => false,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn SetCloak(hwnd: HWND, cloak_type: u32, flags: i32) {
    COM_INIT.with(|_| {
        // Aether: never panic when Explorer's immersive shell isn't there
        let Ok(provider) = (unsafe { CoCreateInstance::<_, IServiceProvider>(&CLSID_ImmersiveShell, None, CLSCTX_ALL) }) else {
            tracing::warn!("cloak: Explorer's immersive shell isn't running");
            return;
        };
        let view_collection = get_iapplication_view_collection(&provider);
        let mut view = None;
        unsafe {
            if view_collection.get_view_for_hwnd(hwnd, &mut view).is_err() {
                tracing::error!(
                    "could not get view for hwnd {} due to os error: {}",
                    hwnd.0 as isize,
                    std::io::Error::last_os_error()
                );
            }
        };

        view.map_or_else(
            || {
                tracing::error!("no view was found for {}", hwnd.0 as isize);
            },
            |view| {
                unsafe {
                    if view.set_cloak(cloak_type, flags).is_err() {
                        tracing::error!(
                            "could not change the cloaking status for hwnd {} due to os error: {}",
                            hwnd.0 as isize,
                            std::io::Error::last_os_error()
                        );
                    }
                };
            },
        );
    });
}
