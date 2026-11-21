//! Aether: overview. Zooms out from the current workspace to every workspace of every monitor,
//! side by side, with live window thumbnails (like Hyprland's expo / niri's overview). Click a
//! workspace or a window to zoom into it; Escape, a right click or the overview key again zooms
//! back into where you were.
//!
//! Like the workspace animations, it is drawn on a topmost overlay with DWM thumbnails, so no
//! real window moves while it is open. Windows of hidden workspaces are parked off-screen
//! (see `park.rs`) where DWM keeps drawing them, so their thumbnails are live too.

use crate::DATA_DIR;
use crate::core::Rect;
use crate::core::SocketMessage;
use crate::window_manager::WindowManager;

use crossbeam_channel::Receiver;
use crossbeam_channel::Sender;
use lazy_static::lazy_static;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::io::Write;
use std::sync::OnceLock;
use std::time::Duration;
use std::time::Instant;
use uds_windows::UnixStream;
use windows::Win32::Foundation::COLORREF;
use windows::Win32::Foundation::HWND;
use windows::Win32::Foundation::LPARAM;
use windows::Win32::Foundation::LRESULT;
use windows::Win32::Foundation::POINT;
use windows::Win32::Foundation::RECT;
use windows::Win32::Foundation::WPARAM;
use windows::Win32::Graphics::Dwm::DWM_THUMBNAIL_PROPERTIES;
use windows::Win32::Graphics::Dwm::DWM_TNP_OPACITY;
use windows::Win32::Graphics::Dwm::DWM_TNP_RECTDESTINATION;
use windows::Win32::Graphics::Dwm::DWM_TNP_RECTSOURCE;
use windows::Win32::Graphics::Dwm::DWM_TNP_SOURCECLIENTAREAONLY;
use windows::Win32::Graphics::Dwm::DWM_TNP_VISIBLE;
use windows::Win32::Graphics::Dwm::DWMWA_EXTENDED_FRAME_BOUNDS;
use windows::Win32::Graphics::Dwm::DwmFlush;
use windows::Win32::Graphics::Dwm::DwmGetWindowAttribute;
use windows::Win32::Graphics::Dwm::DwmRegisterThumbnail;
use windows::Win32::Graphics::Dwm::DwmUnregisterThumbnail;
use windows::Win32::Graphics::Dwm::DwmUpdateThumbnailProperties;
use windows::Win32::Graphics::Gdi::BeginPaint;
use windows::Win32::Graphics::Gdi::CreateSolidBrush;
use windows::Win32::Graphics::Gdi::DeleteObject;
use windows::Win32::Graphics::Gdi::EndPaint;
use windows::Win32::Graphics::Gdi::FillRect;
use windows::Win32::Graphics::Gdi::FrameRect;
use windows::Win32::Graphics::Gdi::InvalidateRect;
use windows::Win32::Graphics::Gdi::PAINTSTRUCT;
use windows::Win32::Graphics::Gdi::ScreenToClient;
use windows::Win32::UI::Input::KeyboardAndMouse::ReleaseCapture;
use windows::Win32::UI::Input::KeyboardAndMouse::SetCapture;
use windows::Win32::UI::Input::KeyboardAndMouse::VK_ESCAPE;
use windows::Win32::UI::Input::KeyboardAndMouse::VK_LEFT;
use windows::Win32::UI::Input::KeyboardAndMouse::VK_RETURN;
use windows::Win32::UI::Input::KeyboardAndMouse::VK_RIGHT;
use windows::Win32::UI::Input::KeyboardAndMouse::VK_SPACE;
use windows::Win32::UI::WindowsAndMessaging::CreateWindowExW;
use windows::Win32::UI::WindowsAndMessaging::DefWindowProcW;
use windows::Win32::UI::WindowsAndMessaging::DestroyWindow;
use windows::Win32::UI::WindowsAndMessaging::DispatchMessageW;
use windows::Win32::UI::WindowsAndMessaging::GetWindowRect;
use windows::Win32::UI::WindowsAndMessaging::HWND_TOPMOST;
use windows::Win32::UI::WindowsAndMessaging::MSG;
use windows::Win32::UI::WindowsAndMessaging::PM_REMOVE;
use windows::Win32::UI::WindowsAndMessaging::PeekMessageW;
use windows::Win32::UI::WindowsAndMessaging::SWP_SHOWWINDOW;
use windows::Win32::UI::WindowsAndMessaging::SetForegroundWindow;
use windows::Win32::UI::WindowsAndMessaging::SetWindowPos;
use windows::Win32::UI::WindowsAndMessaging::TranslateMessage;
use windows::Win32::UI::WindowsAndMessaging::WM_KEYDOWN;
use windows::Win32::UI::WindowsAndMessaging::WM_LBUTTONDOWN;
use windows::Win32::UI::WindowsAndMessaging::WM_LBUTTONUP;
use windows::Win32::UI::WindowsAndMessaging::WM_MOUSEMOVE;
use windows::Win32::UI::WindowsAndMessaging::WM_MOUSEWHEEL;
use windows::Win32::UI::WindowsAndMessaging::WM_PAINT;
use windows::Win32::UI::WindowsAndMessaging::WM_RBUTTONDOWN;
use windows::Win32::UI::WindowsAndMessaging::WNDCLASSW;
use windows::Win32::UI::WindowsAndMessaging::WS_EX_TOOLWINDOW;
use windows::Win32::UI::WindowsAndMessaging::WS_EX_TOPMOST;
use windows::Win32::UI::WindowsAndMessaging::WS_POPUP;
use windows::core::w;

const ZOOM: Duration = Duration::from_millis(320);
const GAP: i32 = 28;
/// Thumb::workspace of the full-screen wallpaper behind the cards
const BACKDROP: usize = usize::MAX;
/// how much the backdrop wallpaper dims when zoomed out (0-255, over the dark background)
const BACKDROP_DIM: f64 = 90.0;
const BACKGROUND: u32 = 0x0014_1010; // COLORREF is 0x00BBGGRR
const HIGHLIGHT: u32 = 0x00FF_FFFF;

/// A window as the overview draws it: its frame rect when its workspace is shown
#[derive(Clone, Debug)]
pub struct WindowView {
    pub hwnd: isize,
    pub rect: Rect,
}

#[derive(Clone, Debug)]
pub struct WorkspaceView {
    pub windows: Vec<WindowView>,
    pub focused: Option<isize>,
}

#[derive(Clone, Debug)]
pub struct MonitorView {
    /// the monitor with keyboard focus (gets the keys while the overview is open)
    pub focused: bool,
    pub size: Rect,
    pub work_area: Rect,
    pub focused_workspace: usize,
    pub workspaces: Vec<WorkspaceView>,
}

enum Job {
    Toggle(Vec<MonitorView>),
}

enum Input {
    Click(isize, POINT),
    Cancel,
    /// mouse wheel: (overlay, wheel delta, cursor in overlay coordinates)
    Wheel(isize, i32, POINT),
    /// drag on the canvas: (overlay, dx, dy)
    Pan(isize, i32, i32),
    /// left / right (arrows, H/L, A/D): move the highlight to the previous / next workspace
    Step(isize, i32),
    /// Enter / Space: open the highlighted workspace
    Choose(isize),
}

/// a left button press: (overlay, where it started, last point, moved far enough to be a drag)
struct Drag {
    overlay: isize,
    start: POINT,
    last: POINT,
    moved: bool,
}

lazy_static! {
    static ref INPUT: Mutex<Vec<Input>> = Mutex::new(vec![]);
    static ref DRAG: Mutex<Option<Drag>> = Mutex::new(None);
    /// overlay -> (tile rects in overlay coordinates, index of the highlighted tile)
    static ref PAINT: Mutex<HashMap<isize, (Vec<RECT>, usize)>> = Mutex::new(HashMap::new());
}

static OVERVIEW: OnceLock<Sender<Job>> = OnceLock::new();

/// Opens the overview, or closes it when it is open
pub fn toggle(wm: &WindowManager) {
    let snapshot = snapshot(wm);
    let sender = OVERVIEW.get_or_init(|| {
        let (sender, receiver) = crossbeam_channel::unbounded();
        std::thread::spawn(move || run(receiver));
        sender
    });
    let _ = sender.send(Job::Toggle(snapshot));
}

fn snapshot(wm: &WindowManager) -> Vec<MonitorView> {
    let mut monitors = vec![];
    let focused_monitor = wm.monitors.focused_idx();
    for (m_idx, monitor) in wm.monitors.elements().iter().enumerate() {
        let work_area = monitor.work_area_size;
        let focused_workspace = monitor.focused_workspace_idx();
        let mut workspaces = vec![];

        for (idx, workspace) in monitor.workspaces.elements().iter().enumerate() {
            let shown = idx == focused_workspace;
            let mut windows = vec![];

            for (c_idx, container) in workspace.containers.elements().iter().enumerate() {
                let Some(window) = container.focused_window() else {
                    continue;
                };
                // the shown workspace: where the window really is; a hidden one: its tile
                let rect = if shown {
                    frame(window.hwnd).unwrap_or_default()
                } else {
                    workspace.latest_layout.get(c_idx).copied().unwrap_or_default()
                };
                windows.push(WindowView { hwnd: window.hwnd, rect });
            }

            if let Some(monocle) = &workspace.monocle_container
                && let Some(window) = monocle.focused_window()
            {
                windows.push(WindowView { hwnd: window.hwnd, rect: work_area });
            }

            for window in workspace.floating_windows.elements() {
                let rect = if shown {
                    frame(window.hwnd).unwrap_or_default()
                } else {
                    parked_rect(window.hwnd).unwrap_or_default()
                };
                windows.push(WindowView { hwnd: window.hwnd, rect });
            }

            let focused = workspace
                .focused_container()
                .and_then(|c| c.focused_window())
                .map(|w| w.hwnd);
            windows.retain(|w| w.rect.right > 0 && w.rect.bottom > 0);
            workspaces.push(WorkspaceView { windows, focused });
        }

        monitors.push(MonitorView {
            focused: m_idx == focused_monitor,
            size: monitor.size,
            work_area,
            focused_workspace,
            workspaces,
        });
    }
    monitors
}

/// frame (no invisible resize borders) of a window as komorebi's Rect (left, top, width, height)
fn frame(hwnd: isize) -> Option<Rect> {
    let mut r = RECT::default();
    unsafe {
        DwmGetWindowAttribute(
            HWND(hwnd as _),
            DWMWA_EXTENDED_FRAME_BOUNDS,
            (&raw mut r).cast(),
            std::mem::size_of::<RECT>() as u32,
        )
    }
    .ok()?;
    Some(Rect { left: r.left, top: r.top, right: r.right - r.left, bottom: r.bottom - r.top })
}

/// where a parked floating window will be when its workspace is shown again
fn parked_rect(hwnd: isize) -> Option<Rect> {
    let (x, y) = crate::park::original(hwnd)?;
    let f = frame(hwnd)?;
    Some(Rect { left: x, top: y, right: f.right, bottom: f.bottom })
}

fn raw_rect(hwnd: isize) -> Option<RECT> {
    let mut r = RECT::default();
    unsafe { GetWindowRect(HWND(hwnd as _), &mut r) }.ok()?;
    Some(r)
}

fn send(message: &SocketMessage) {
    if let Ok(bytes) = message.as_bytes()
        && let Ok(mut stream) = UnixStream::connect(DATA_DIR.join("komorebi.sock"))
    {
        let _ = stream.write_all(&bytes);
    }
}

fn point_of(lparam: LPARAM) -> POINT {
    POINT { x: (lparam.0 & 0xFFFF) as i16 as i32, y: ((lparam.0 >> 16) & 0xFFFF) as i16 as i32 }
}

extern "system" fn callback(hwnd: HWND, message: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    match message {
        // a press and release without moving is a click; moving while pressed pans the canvas
        WM_LBUTTONDOWN => {
            let point = point_of(lparam);
            unsafe {
                let _ = SetCapture(hwnd);
            }
            *DRAG.lock() = Some(Drag { overlay: hwnd.0 as isize, start: point, last: point, moved: false });
            LRESULT(0)
        }
        WM_MOUSEMOVE => {
            let point = point_of(lparam);
            let mut drag = DRAG.lock();
            if let Some(d) = drag.as_mut()
                && d.overlay == hwnd.0 as isize
            {
                if (point.x - d.start.x).abs() + (point.y - d.start.y).abs() > 6 {
                    d.moved = true;
                }
                if d.moved {
                    INPUT.lock().push(Input::Pan(d.overlay, point.x - d.last.x, point.y - d.last.y));
                }
                d.last = point;
            }
            LRESULT(0)
        }
        WM_LBUTTONUP => {
            let point = point_of(lparam);
            unsafe {
                let _ = ReleaseCapture();
            }
            if let Some(d) = DRAG.lock().take()
                && !d.moved
            {
                INPUT.lock().push(Input::Click(hwnd.0 as isize, point));
            }
            LRESULT(0)
        }
        WM_MOUSEWHEEL => {
            let delta = ((wparam.0 >> 16) & 0xFFFF) as i16 as i32;
            // the wheel message carries screen coordinates
            let mut point = point_of(lparam);
            unsafe {
                let _ = ScreenToClient(hwnd, &mut point);
            }
            INPUT.lock().push(Input::Wheel(hwnd.0 as isize, delta, point));
            LRESULT(0)
        }
        WM_RBUTTONDOWN => {
            INPUT.lock().push(Input::Cancel);
            LRESULT(0)
        }
        WM_KEYDOWN if wparam.0 as u16 == VK_ESCAPE.0 => {
            INPUT.lock().push(Input::Cancel);
            LRESULT(0)
        }
        WM_KEYDOWN => {
            let key = wparam.0 as u16;
            let overlay = hwnd.0 as isize;
            if key == VK_LEFT.0 || key == 0x48 || key == 0x41 {
                INPUT.lock().push(Input::Step(overlay, -1));
            } else if key == VK_RIGHT.0 || key == 0x4C || key == 0x44 {
                INPUT.lock().push(Input::Step(overlay, 1));
            } else if key == VK_RETURN.0 || key == VK_SPACE.0 {
                INPUT.lock().push(Input::Choose(overlay));
            }
            LRESULT(0)
        }
        WM_PAINT => {
            let mut ps = PAINTSTRUCT::default();
            unsafe {
                let hdc = BeginPaint(hwnd, &mut ps);
                let background = CreateSolidBrush(COLORREF(BACKGROUND));
                FillRect(hdc, &ps.rcPaint, background);
                let _ = DeleteObject(background.into());
                if let Some((tiles, highlighted)) = PAINT.lock().get(&(hwnd.0 as isize))
                    && let Some(tile) = tiles.get(*highlighted)
                {
                    let brush = CreateSolidBrush(COLORREF(HIGHLIGHT));
                    for grow in 2..5 {
                        let r = RECT {
                            left: tile.left - grow,
                            top: tile.top - grow,
                            right: tile.right + grow,
                            bottom: tile.bottom + grow,
                        };
                        FrameRect(hdc, &r, brush);
                    }
                    let _ = DeleteObject(brush.into());
                }
                let _ = EndPaint(hwnd, &ps);
            }
            LRESULT(0)
        }
        _ => unsafe { DefWindowProcW(hwnd, message, wparam, lparam) },
    }
}

fn create_overlay(size: Rect) -> Option<isize> {
    let class_name = w!("komorebi-aether-overview");
    let h_module = crate::windows_api::WindowsApi::module_handle_w().ok()?;
    let class = WNDCLASSW {
        hInstance: h_module.into(),
        lpszClassName: class_name,
        lpfnWndProc: Some(callback),
        ..Default::default()
    };
    let _ = crate::windows_api::WindowsApi::register_class_w(&class);

    let hwnd = unsafe {
        CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            class_name,
            class_name,
            WS_POPUP,
            size.left,
            size.top,
            size.right,
            size.bottom,
            None,
            None,
            Some(h_module.into()),
            None,
        )
    }
    .ok()?;
    Some(hwnd.0 as isize)
}

fn pump() {
    let mut msg = MSG::default();
    unsafe {
        while PeekMessageW(&mut msg, None, 0, 0, PM_REMOVE).as_bool() {
            let _ = TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

fn update(id: isize, destination: RECT, source: RECT) {
    update_with_opacity(id, destination, source, 255);
}

fn update_with_opacity(id: isize, destination: RECT, source: RECT, opacity: u8) {
    let properties = DWM_THUMBNAIL_PROPERTIES {
        dwFlags: DWM_TNP_RECTDESTINATION
            | DWM_TNP_RECTSOURCE
            | DWM_TNP_OPACITY
            | DWM_TNP_VISIBLE
            | DWM_TNP_SOURCECLIENTAREAONLY,
        rcDestination: destination,
        rcSource: source,
        opacity,
        fVisible: true.into(),
        fSourceClientAreaOnly: false.into(),
    };
    let _ = unsafe { DwmUpdateThumbnailProperties(id, &properties) };
}

fn ease_out_cubic(x: f64) -> f64 {
    1.0 - (1.0 - x.clamp(0.0, 1.0)).powi(3)
}

struct Thumb {
    id: isize,
    /// rect inside its workspace tile, relative to the work area, at full size
    rect: Rect,
    /// the visible frame inside the window (crops the invisible resize borders)
    source: RECT,
    workspace: usize,
    hwnd: Option<isize>,
}

struct Screen {
    overlay: isize,
    view: MonitorView,
    /// tile rects in overlay coordinates, one per workspace
    tiles: Vec<RECT>,
    scale: f64,
    thumbs: Vec<Thumb>,
    /// which tile the zoom starts from / ends in
    zoom_into: usize,
    /// the free camera over the zoomed-out canvas (mouse wheel zooms around the cursor, dragging pans):
    /// a point p of the canvas is drawn at (p - centre) * zoom + centre + pan
    zoom: f64,
    pan: (f64, f64),
}

impl Screen {
    fn new(view: MonitorView) -> Option<Self> {
        let overlay = create_overlay(view.size)?;
        let n = view.workspaces.len().max(1) as i32;
        let (w, h) = (view.size.right, view.size.bottom);
        let wa = view.work_area;
        let tile_w = (w - GAP * (n + 1)) / n;
        let scale = tile_w as f64 / wa.right.max(1) as f64;
        let tile_h = (wa.bottom as f64 * scale) as i32;
        let top = (h - tile_h) / 2;
        let tiles = (0..n)
            .map(|i| RECT {
                left: GAP + i * (tile_w + GAP),
                top,
                right: GAP + i * (tile_w + GAP) + tile_w,
                bottom: top + tile_h,
            })
            .collect::<Vec<_>>();

        let mut thumbs = vec![];
        // the wallpaper behind everything, over the whole monitor (like niri): dims a little when zoomed out
        if let Some(wallpaper) = crate::animation::workspace::wallpaper_for(view.size)
            && let Ok(id) = unsafe { DwmRegisterThumbnail(HWND(overlay as _), HWND(wallpaper as _)) }
            && let Some(raw) = raw_rect(wallpaper)
        {
            thumbs.push(Thumb {
                id,
                rect: Rect { left: 0, top: 0, right: w, bottom: h },
                source: RECT {
                    left: view.size.left - raw.left,
                    top: view.size.top - raw.top,
                    right: view.size.left - raw.left + w,
                    bottom: view.size.top - raw.top + h,
                },
                workspace: BACKDROP,
                hwnd: None,
            });
        }
        for (ws_idx, workspace) in view.workspaces.iter().enumerate() {
            // the wallpaper behind each workspace
            if let Some(wallpaper) = crate::animation::workspace::wallpaper_for(wa)
                && let Ok(id) = unsafe { DwmRegisterThumbnail(HWND(overlay as _), HWND(wallpaper as _)) }
                && let Some(raw) = raw_rect(wallpaper)
            {
                thumbs.push(Thumb {
                    id,
                    rect: Rect { left: 0, top: 0, right: wa.right, bottom: wa.bottom },
                    source: RECT {
                        left: wa.left - raw.left,
                        top: wa.top - raw.top,
                        right: wa.left - raw.left + wa.right,
                        bottom: wa.top - raw.top + wa.bottom,
                    },
                    workspace: ws_idx,
                    hwnd: None,
                });
            }

            for window in &workspace.windows {
                let Ok(id) = (unsafe { DwmRegisterThumbnail(HWND(overlay as _), HWND(window.hwnd as _)) })
                else {
                    continue;
                };
                let raw = raw_rect(window.hwnd).unwrap_or_default();
                let f = frame(window.hwnd).unwrap_or(Rect {
                    left: raw.left,
                    top: raw.top,
                    right: raw.right - raw.left,
                    bottom: raw.bottom - raw.top,
                });
                thumbs.push(Thumb {
                    id,
                    rect: Rect {
                        left: window.rect.left - wa.left,
                        top: window.rect.top - wa.top,
                        right: window.rect.right,
                        bottom: window.rect.bottom,
                    },
                    source: RECT {
                        left: f.left - raw.left,
                        top: f.top - raw.top,
                        right: f.left - raw.left + f.right,
                        bottom: f.top - raw.top + f.bottom,
                    },
                    workspace: ws_idx,
                    hwnd: Some(window.hwnd),
                });
            }
        }

        let zoom_into = view.focused_workspace;
        Some(Self { overlay, view, tiles, scale, thumbs, zoom_into, zoom: 1.0, pan: (0.0, 0.0) })
    }

    fn centre(&self) -> (f64, f64) {
        (self.view.size.right as f64 / 2.0, self.view.size.bottom as f64 / 2.0)
    }

    /// canvas -> screen with the camera applied by `e` (so the zoom in/out animations still start and end
    /// on the real screen)
    fn camera(&self, x: f64, y: f64, e: f64) -> (f64, f64) {
        let (cx, cy) = self.centre();
        let z = 1.0 + (self.zoom - 1.0) * e;
        ((x - cx) * z + cx + self.pan.0 * e, (y - cy) * z + cy + self.pan.1 * e)
    }

    /// screen -> canvas (fully zoomed out)
    fn uncamera(&self, point: POINT) -> POINT {
        let (cx, cy) = self.centre();
        POINT {
            x: ((point.x as f64 - cx - self.pan.0) / self.zoom + cx).round() as i32,
            y: ((point.y as f64 - cy - self.pan.1) / self.zoom + cy).round() as i32,
        }
    }

    /// the wheel: zoom around the cursor, keeping what is under it under it
    fn wheel(&mut self, delta: i32, cursor: POINT) {
        let world = self.uncamera(cursor);
        let zoom = (self.zoom * 1.15f64.powf(delta as f64 / 120.0)).clamp(0.35, 8.0);
        let (cx, cy) = self.centre();
        self.zoom = zoom;
        self.pan = (
            cursor.x as f64 - cx - (world.x as f64 - cx) * zoom,
            cursor.y as f64 - cy - (world.y as f64 - cy) * zoom,
        );
        self.place(1.0);
    }

    fn drag(&mut self, dx: i32, dy: i32) {
        self.pan.0 += dx as f64;
        self.pan.1 += dy as f64;
        self.place(1.0);
    }

    /// e = 0: the tile `zoom_into` fills the work area (what was on screen); e = 1: all tiles
    fn place(&self, e: f64) {
        let Some(home) = self.tiles.get(self.zoom_into).copied() else {
            return;
        };
        let wa = self.view.work_area;
        let origin_x = (wa.left - self.view.size.left) as f64;
        let origin_y = (wa.top - self.view.size.top) as f64;
        let inv = 1.0 / self.scale;
        // a point of the zoomed-out picture -> where it is at progress e
        let map = |x: f64, y: f64| {
            let sx = origin_x + (x - home.left as f64) * inv;
            let sy = origin_y + (y - home.top as f64) * inv;
            (sx + (x - sx) * e, sy + (y - sy) * e)
        };
        let to_rect = |l: f64, t: f64, r: f64, b: f64| {
            let (l, t) = map(l, t);
            let (r, b) = map(r, b);
            let (l, t) = self.camera(l, t, e);
            let (r, b) = self.camera(r, b, e);
            RECT { left: l.round() as i32, top: t.round() as i32, right: r.round() as i32, bottom: b.round() as i32 }
        };

        let tiles: Vec<RECT> = self
            .tiles
            .iter()
            .map(|t| to_rect(t.left as f64, t.top as f64, t.right as f64, t.bottom as f64))
            .collect();

        for thumb in &self.thumbs {
            if thumb.workspace == BACKDROP {
                let full = RECT { left: 0, top: 0, right: thumb.rect.right, bottom: thumb.rect.bottom };
                update_with_opacity(thumb.id, full, thumb.source, (255.0 - BACKDROP_DIM * e).round() as u8);
                continue;
            }
            let Some(tile) = self.tiles.get(thumb.workspace) else {
                continue;
            };
            let l = tile.left as f64 + thumb.rect.left as f64 * self.scale;
            let t = tile.top as f64 + thumb.rect.top as f64 * self.scale;
            let r = l + thumb.rect.right as f64 * self.scale;
            let b = t + thumb.rect.bottom as f64 * self.scale;
            update(thumb.id, to_rect(l, t, r, b), thumb.source);
        }

        PAINT.lock().insert(self.overlay, (tiles, self.zoom_into));
        unsafe {
            let _ = InvalidateRect(Some(HWND(self.overlay as _)), None, false);
        }
    }

    /// (workspace, window) under a point of the overlay, when zoomed out
    fn hit(&self, point: POINT) -> Option<(usize, Option<isize>)> {
        let point = self.uncamera(point);
        let ws = self.tiles.iter().position(|t| {
            point.x >= t.left && point.x < t.right && point.y >= t.top && point.y < t.bottom
        })?;
        let tile = self.tiles[ws];
        // topmost window first
        let window = self.thumbs.iter().rev().find_map(|thumb| {
            let hwnd = thumb.hwnd?;
            if thumb.workspace != ws {
                return None;
            }
            let l = tile.left + (thumb.rect.left as f64 * self.scale) as i32;
            let t = tile.top + (thumb.rect.top as f64 * self.scale) as i32;
            let r = l + (thumb.rect.right as f64 * self.scale) as i32;
            let b = t + (thumb.rect.bottom as f64 * self.scale) as i32;
            (point.x >= l && point.x < r && point.y >= t && point.y < b).then_some(hwnd)
        });
        Some((ws, window))
    }

    fn destroy(self) {
        for thumb in &self.thumbs {
            let _ = unsafe { DwmUnregisterThumbnail(thumb.id) };
        }
        PAINT.lock().remove(&self.overlay);
        unsafe {
            let _ = DestroyWindow(HWND(self.overlay as _));
        }
    }
}

fn animate(screens: &[Screen], zoom_out: bool) {
    let start = Instant::now();
    loop {
        let p = start.elapsed().as_secs_f64() / ZOOM.as_secs_f64();
        let e = ease_out_cubic(p);
        let e = if zoom_out { e } else { 1.0 - e };
        for screen in screens {
            screen.place(e);
        }
        pump();
        let _ = unsafe { DwmFlush() };
        if p >= 1.0 {
            break;
        }
    }
}

fn run(receiver: Receiver<Job>) {
    while let Ok(job) = receiver.recv() {
        let Job::Toggle(views) = job;
        let mut screens: Vec<Screen> = views.into_iter().filter_map(Screen::new).collect();
        if screens.is_empty() {
            continue;
        }

        // show at e = 0 (identical to the screen), then zoom out
        for screen in &screens {
            screen.place(0.0);
            unsafe {
                let _ = SetWindowPos(
                    HWND(screen.overlay as _),
                    Some(HWND_TOPMOST),
                    screen.view.size.left,
                    screen.view.size.top,
                    screen.view.size.right,
                    screen.view.size.bottom,
                    SWP_SHOWWINDOW,
                );
            }
        }
        let keys = screens.iter().find(|s| s.view.focused).map_or(screens[0].overlay, |s| s.overlay);
        unsafe {
            let _ = SetForegroundWindow(HWND(keys as _));
        }
        animate(&screens, true);
        INPUT.lock().clear();

        // wait for a choice
        let mut chosen: Option<(usize, usize, Option<isize>)> = None;
        'wait: loop {
            pump();
            if receiver.try_recv().is_ok() {
                break 'wait; // the overview key again: back where we were
            }
            let inputs = std::mem::take(&mut *INPUT.lock());
            for input in inputs {
                match input {
                    Input::Cancel => break 'wait,
                    Input::Wheel(overlay, delta, point) => {
                        if let Some(screen) = screens.iter_mut().find(|s| s.overlay == overlay) {
                            screen.wheel(delta, point);
                        }
                    }
                    Input::Pan(overlay, dx, dy) => {
                        if let Some(screen) = screens.iter_mut().find(|s| s.overlay == overlay) {
                            screen.drag(dx, dy);
                        }
                    }
                    Input::Step(overlay, step) => {
                        if let Some(screen) = screens.iter_mut().find(|s| s.overlay == overlay) {
                            let last = screen.tiles.len().saturating_sub(1) as i32;
                            screen.zoom_into = (screen.zoom_into as i32 + step).clamp(0, last) as usize;
                            screen.place(1.0);
                        }
                    }
                    Input::Choose(overlay) => {
                        if let Some(m) = screens.iter().position(|s| s.overlay == overlay) {
                            chosen = Some((m, screens[m].zoom_into, None));
                            break 'wait;
                        }
                    }
                    Input::Click(overlay, point) => {
                        if let Some(m) = screens.iter().position(|s| s.overlay == overlay)
                            && let Some((ws, window)) = screens[m].hit(point)
                        {
                            chosen = Some((m, ws, window));
                            break 'wait;
                        }
                        // a click outside the tiles closes it
                        break 'wait;
                    }
                }
            }
            std::thread::sleep(Duration::from_millis(8));
        }

        // switch underneath the overlay, then zoom into the chosen workspace
        if let Some((m, ws, _)) = chosen {
            send(&SocketMessage::FocusMonitorWorkspaceNumber(m, ws));
            screens[m].zoom_into = ws;
        }
        animate(&screens, false);
        if let Some((_, _, Some(hwnd))) = chosen {
            unsafe {
                let _ = SetForegroundWindow(HWND(hwnd as _));
            }
        }
        for screen in screens.drain(..) {
            screen.destroy();
        }
        // toggles which came in while it was closing
        while receiver.try_recv().is_ok() {}
    }
}
