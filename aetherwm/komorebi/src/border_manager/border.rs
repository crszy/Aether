use crate::WINDOWS_11;
use crate::WindowsApi;
use crate::border_manager::BORDER_OFFSET;
use crate::border_manager::BORDER_WIDTH;
use crate::border_manager::RenderTarget;
use crate::border_manager::STYLE;
use crate::border_manager::WindowKind;
use crate::border_manager::window_kind_colour;
use crate::core::BorderStyle;
use crate::core::Rect;
use crate::windows_api;
use std::collections::HashMap;
use std::ops::Deref;
use std::sync::Arc;
use std::sync::LazyLock;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::Ordering;
use std::sync::mpsc;
use windows::Win32::Foundation::FALSE;
use windows::Win32::Foundation::HWND;
use windows::Win32::Foundation::LPARAM;
use windows::Win32::Foundation::LRESULT;
use windows::Win32::Foundation::TRUE;
use windows::Win32::Foundation::WPARAM;
use windows::Win32::Graphics::Direct2D::Common::D2D_RECT_F;
use windows::Win32::Graphics::Direct2D::Common::D2D_SIZE_U;
use windows::Win32::Graphics::Direct2D::Common::D2D1_ALPHA_MODE_PREMULTIPLIED;
use windows::Win32::Graphics::Direct2D::Common::D2D1_COLOR_F;
use windows::Win32::Graphics::Direct2D::Common::D2D1_PIXEL_FORMAT;
use windows::Win32::Graphics::Direct2D::D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
use windows::Win32::Graphics::Direct2D::D2D1_BRUSH_PROPERTIES;
use windows::Win32::Graphics::Direct2D::D2D1_FACTORY_TYPE_MULTI_THREADED;
use windows::Win32::Graphics::Direct2D::D2D1_HWND_RENDER_TARGET_PROPERTIES;
use windows::Win32::Graphics::Direct2D::D2D1_PRESENT_OPTIONS_IMMEDIATELY;
use windows::Win32::Graphics::Direct2D::D2D1_RENDER_TARGET_PROPERTIES;
use windows::Win32::Graphics::Direct2D::D2D1_RENDER_TARGET_TYPE_DEFAULT;
use windows::Win32::Graphics::Direct2D::D2D1_ROUNDED_RECT;
use windows::Win32::Graphics::Direct2D::D2D1CreateFactory;
use windows::Win32::Graphics::Direct2D::ID2D1Factory;
use windows::Win32::Graphics::Direct2D::ID2D1SolidColorBrush;
use windows::Win32::Graphics::Dwm::DWM_BB_BLURREGION;
use windows::Win32::Graphics::Dwm::DWM_BB_ENABLE;
use windows::Win32::Graphics::Dwm::DWM_BLURBEHIND;
use windows::Win32::Graphics::Dwm::DwmEnableBlurBehindWindow;
use windows::Win32::Graphics::Dxgi::Common::DXGI_FORMAT_UNKNOWN;
use windows::Win32::Graphics::Gdi::CreateRectRgn;
use windows::Win32::Graphics::Gdi::InvalidateRect;
use windows::Win32::Graphics::Gdi::ValidateRect;
use windows::Win32::UI::WindowsAndMessaging::CREATESTRUCTW;
use windows::Win32::UI::WindowsAndMessaging::DefWindowProcW;
use windows::Win32::UI::WindowsAndMessaging::DispatchMessageW;
use windows::Win32::UI::WindowsAndMessaging::EVENT_OBJECT_DESTROY;
use windows::Win32::UI::WindowsAndMessaging::EVENT_OBJECT_LOCATIONCHANGE;
use windows::Win32::UI::WindowsAndMessaging::GWLP_USERDATA;
use windows::Win32::UI::WindowsAndMessaging::GetMessageW;
use windows::Win32::UI::WindowsAndMessaging::GetSystemMetrics;
use windows::Win32::UI::WindowsAndMessaging::GetWindowLongPtrW;
use windows::Win32::UI::WindowsAndMessaging::IDC_ARROW;
use windows::Win32::UI::WindowsAndMessaging::LoadCursorW;
use windows::Win32::UI::WindowsAndMessaging::MSG;
use windows::Win32::UI::WindowsAndMessaging::PostMessageW;
use windows::Win32::UI::WindowsAndMessaging::PostQuitMessage;
use windows::Win32::UI::WindowsAndMessaging::SM_CXVIRTUALSCREEN;
use windows::Win32::UI::WindowsAndMessaging::SetCursor;
use windows::Win32::UI::WindowsAndMessaging::SetWindowLongPtrW;
use windows::Win32::UI::WindowsAndMessaging::TranslateMessage;
use windows::Win32::UI::WindowsAndMessaging::WM_CREATE;
use windows::Win32::UI::WindowsAndMessaging::WM_DESTROY;
use windows::Win32::UI::WindowsAndMessaging::WM_PAINT;
use windows::Win32::UI::WindowsAndMessaging::WM_SETCURSOR;
use windows::Win32::UI::WindowsAndMessaging::WM_USER;
use windows::Win32::UI::WindowsAndMessaging::WNDCLASSW;
use windows_core::BOOL;
use windows_core::PCWSTR;
use windows_numerics::Matrix3x2;

/// Custom WM_USER message that tells the border window thread to call update_brushes() on itself,
/// avoiding a data race between the border manager thread and the border's message loop thread.
pub const WM_UPDATE_BRUSHES: u32 = WM_USER + 1;

pub struct RenderFactory(ID2D1Factory);
unsafe impl Sync for RenderFactory {}
unsafe impl Send for RenderFactory {}

impl Deref for RenderFactory {
    type Target = ID2D1Factory;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

#[allow(clippy::expect_used)]
static RENDER_FACTORY: LazyLock<RenderFactory> = unsafe {
    LazyLock::new(|| {
        RenderFactory(
            D2D1CreateFactory::<ID2D1Factory>(D2D1_FACTORY_TYPE_MULTI_THREADED, None)
                .expect("creating RENDER_FACTORY failed"),
        )
    })
};

static BRUSH_PROPERTIES: LazyLock<D2D1_BRUSH_PROPERTIES> =
    LazyLock::new(|| D2D1_BRUSH_PROPERTIES {
        opacity: 1.0,
        transform: Matrix3x2::identity(),
    });

pub extern "system" fn border_hwnds(hwnd: HWND, lparam: LPARAM) -> BOOL {
    let hwnds = unsafe { &mut *(lparam.0 as *mut Vec<isize>) };
    let hwnd = hwnd.0 as isize;

    if let Ok(class) = WindowsApi::real_window_class_w(hwnd)
        && class.starts_with("komoborder")
    {
        hwnds.push(hwnd);
    }

    true.into()
}

#[derive(Debug, Clone)]
pub struct Border {
    pub hwnd: isize,
    pub id: String,
    pub monitor_idx: Option<usize>,
    pub render_target: Option<RenderTarget>,
    pub tracking_hwnd: isize,
    pub window_rect: Rect,
    pub window_kind: WindowKind,
    pub style: BorderStyle,
    pub width: i32,
    pub offset: i32,
    pub brush_properties: D2D1_BRUSH_PROPERTIES,
    pub rounded_rect: D2D1_ROUNDED_RECT,
    pub brushes: HashMap<WindowKind, ID2D1SolidColorBrush>,
    pub is_destroying: Arc<AtomicBool>,
    /// Aether: the kind drawn last and when a focus fade started (from that kind's colour)
    pub drawn_kind: WindowKind,
    /// a running focus fade: when it started and the colour it started from
    pub fade: Option<(std::time::Instant, (f32, f32, f32))>,
}

/// Aether: hyprland.conf gradient border (col.active_border with 2+ colours and an angle) and its spin
/// (animation = borderangle, 1, speed, curve, loop). Only the focused border spins, at SPIN_FPS, and not while a
/// fullscreen app / game has focus.
pub struct Gradient {
    pub colours: Vec<(f32, f32, f32, f32)>,
    pub angle: f32,
}
pub static GRADIENT: std::sync::Mutex<Option<Gradient>> = std::sync::Mutex::new(None);
/// ms for one turn; 0 = no spin
pub static SPIN_MS: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
pub static SPIN_REVERSE: AtomicBool = AtomicBool::new(false);
pub static SPIN_FPS: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(30);
static FOCUSED_BORDER: std::sync::atomic::AtomicIsize = std::sync::atomic::AtomicIsize::new(0);
static SPIN_START: std::sync::LazyLock<std::time::Instant> = std::sync::LazyLock::new(std::time::Instant::now);
static SPIN_THREAD: std::sync::Once = std::sync::Once::new();

/// starts the (single) spin ticker; it sleeps while spinning is off
pub fn ensure_spin_ticker() {
    SPIN_THREAD.call_once(|| {
        std::thread::spawn(|| loop {
            let ms = SPIN_MS.load(Ordering::Relaxed);
            if ms == 0 {
                std::thread::sleep(std::time::Duration::from_millis(400));
                continue;
            }
            let fps = SPIN_FPS.load(Ordering::Relaxed).clamp(5, 120);
            std::thread::sleep(std::time::Duration::from_millis(1000 / fps));
            // a game / fullscreen app has focus: nothing spins
            if let Ok(fg) = WindowsApi::foreground_window()
                && crate::hypr::leave_alone(fg)
            {
                continue;
            }
            let hwnd = FOCUSED_BORDER.load(Ordering::Relaxed);
            if hwnd != 0 {
                let _ = unsafe { InvalidateRect(Option::from(HWND(windows_api::as_ptr!(hwnd))), None, false) };
            }
        });
    });
}

/// the gradient brush for a focused border, or None (unfocused, no gradient, or mid-fade)
unsafe fn gradient_paint(
    rt: &windows::Win32::Graphics::Direct2D::ID2D1HwndRenderTarget,
    border: &Border,
    kind: WindowKind,
    faded: bool,
    w: f32,
    h: f32,
) -> Option<windows::Win32::Graphics::Direct2D::ID2D1Brush> {
    use windows_numerics::Vector2 as D2D_POINT_2F;
    use windows::Win32::Graphics::Direct2D::D2D1_EXTEND_MODE_CLAMP;
    use windows::Win32::Graphics::Direct2D::D2D1_GAMMA_2_2;
    use windows::Win32::Graphics::Direct2D::Common::D2D1_GRADIENT_STOP;
    use windows::Win32::Graphics::Direct2D::D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES;
    use windows_core::Interface;
    if faded || matches!(kind, WindowKind::Unfocused | WindowKind::UnfocusedLocked) {
        return None;
    }
    let guard = GRADIENT.lock().ok()?;
    let g = guard.as_ref()?;
    if g.colours.len() < 2 {
        return None;
    }
    FOCUSED_BORDER.store(border.hwnd, Ordering::Relaxed);
    let spin = SPIN_MS.load(Ordering::Relaxed);
    let mut angle = g.angle;
    if spin > 0 {
        let turn = (SPIN_START.elapsed().as_millis() % spin as u128) as f32 / spin as f32 * 360.0;
        angle += if SPIN_REVERSE.load(Ordering::Relaxed) { -turn } else { turn };
    }
    let n = g.colours.len();
    let stops: Vec<D2D1_GRADIENT_STOP> = g
        .colours
        .iter()
        .enumerate()
        .map(|(i, c)| D2D1_GRADIENT_STOP { position: i as f32 / (n - 1) as f32, color: D2D1_COLOR_F { r: c.0, g: c.1, b: c.2, a: c.3 } })
        .collect();
    let collection = unsafe { rt.CreateGradientStopCollection(&stops, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP) }.ok()?;
    // Hyprland: 0deg runs left to right, angles turn counter-clockwise
    let rad = (-angle).to_radians();
    let (cx, cy) = (w / 2.0, h / 2.0);
    let reach = (w * rad.cos().abs() + h * rad.sin().abs()) / 2.0;
    let props = D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES {
        startPoint: D2D_POINT_2F { X: cx - rad.cos() * reach, Y: cy - rad.sin() * reach },
        endPoint: D2D_POINT_2F { X: cx + rad.cos() * reach, Y: cy + rad.sin() * reach },
    };
    let brush = unsafe { rt.CreateLinearGradientBrush(&props, Some(&border.brush_properties), &collection) }.ok()?;
    brush.cast().ok()
}

/// Aether: focus fade length in ms (hyprland.conf `animation = border, ...`); 0 = off. Only a border whose focus
/// changed redraws, for this long, then nothing runs.
pub static BORDER_FADE_MS: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);

fn colour_f(c: u32) -> (f32, f32, f32) {
    ((c & 0xFF) as f32 / 255.0, ((c >> 8) & 0xFF) as f32 / 255.0, ((c >> 16) & 0xFF) as f32 / 255.0)
}

/// the colour this border draws now; starts a fade (and its redraw ticks) when the kind changed
fn faded_colour(border: &mut Border) -> Option<D2D1_COLOR_F> {
    let ms = BORDER_FADE_MS.load(Ordering::Relaxed);
    let kind = border.window_kind;
    // the colour on screen right now (mid-fade: the in-between one)
    let showing = |border: &Border| -> (f32, f32, f32) {
        match border.fade {
            Some((start, (r0, g0, b0))) => {
                let t = (start.elapsed().as_millis() as f32 / ms.max(1) as f32).min(1.0);
                let e = 1.0 - (1.0 - t).powi(3);
                let (r1, g1, b1) = colour_f(window_kind_colour(border.drawn_kind));
                (r0 + (r1 - r0) * e, g0 + (g1 - g0) * e, b0 + (b1 - b0) * e)
            }
            None => colour_f(window_kind_colour(border.drawn_kind)),
        }
    };
    if kind != border.drawn_kind {
        // focus moved again mid-fade: the new fade starts from the colour on screen, never jumps back
        let from = showing(border);
        border.drawn_kind = kind;
        if ms > 0 && kind != WindowKind::Unfocused && kind != WindowKind::UnfocusedLocked {
            border.fade = Some((std::time::Instant::now(), from));
            let hwnd = border.hwnd;
            std::thread::spawn(move || {
                let end = std::time::Instant::now() + std::time::Duration::from_millis(ms + 20);
                while std::time::Instant::now() < end {
                    std::thread::sleep(std::time::Duration::from_millis(14));
                    let _ = unsafe { InvalidateRect(Option::from(HWND(windows_api::as_ptr!(hwnd))), None, false) };
                }
            });
        } else {
            border.fade = None;
        }
    }
    let (start, _) = border.fade?;
    let (r, g, b) = showing(border);
    if start.elapsed().as_millis() as u64 >= ms {
        border.fade = None;
    }
    Some(D2D1_COLOR_F { r, g, b, a: 1.0 })
}

impl From<isize> for Border {
    fn from(value: isize) -> Self {
        Self {
            hwnd: value,
            id: String::new(),
            monitor_idx: None,
            render_target: None,
            tracking_hwnd: 0,
            window_rect: Rect::default(),
            window_kind: WindowKind::Unfocused,
            style: STYLE.load(),
            width: BORDER_WIDTH.load(Ordering::Relaxed),
            offset: BORDER_OFFSET.load(Ordering::Relaxed),
            brush_properties: D2D1_BRUSH_PROPERTIES::default(),
            rounded_rect: D2D1_ROUNDED_RECT::default(),
            brushes: HashMap::new(),
            is_destroying: Arc::new(AtomicBool::new(false)),
            drawn_kind: WindowKind::Unfocused,
            fade: None,
        }
    }
}

impl Border {
    pub const fn hwnd(&self) -> HWND {
        HWND(windows_api::as_ptr!(self.hwnd))
    }

    pub fn create(
        id: &str,
        tracking_hwnd: isize,
        monitor_idx: usize,
    ) -> color_eyre::Result<Box<Self>> {
        let name: Vec<u16> = format!("komoborder-{id}\0").encode_utf16().collect();
        let class_name = PCWSTR(name.as_ptr());

        let h_module = WindowsApi::module_handle_w()?;

        let window_class = WNDCLASSW {
            hInstance: h_module.into(),
            lpszClassName: class_name,
            lpfnWndProc: Some(Self::callback),
            hbrBackground: WindowsApi::create_solid_brush(0),
            ..Default::default()
        };

        let _ = WindowsApi::register_class_w(&window_class);

        let (border_sender, border_receiver) = mpsc::channel();

        let instance = h_module.0 as isize;
        let container_id = id.to_owned();
        std::thread::spawn(move || -> color_eyre::Result<()> {
            let mut border = Self {
                hwnd: 0,
                id: container_id,
                monitor_idx: Some(monitor_idx),
                render_target: None,
                tracking_hwnd,
                window_rect: WindowsApi::window_rect(tracking_hwnd).unwrap_or_default(),
                window_kind: WindowKind::Unfocused,
                style: STYLE.load(),
                width: BORDER_WIDTH.load(Ordering::Relaxed),
                offset: BORDER_OFFSET.load(Ordering::Relaxed),
                brush_properties: Default::default(),
                rounded_rect: Default::default(),
                brushes: HashMap::new(),
                is_destroying: Arc::new(AtomicBool::new(false)),
                drawn_kind: WindowKind::Unfocused,
                fade: None,
            };

            let border_pointer = &raw mut border;
            let hwnd =
                WindowsApi::create_border_window(PCWSTR(name.as_ptr()), instance, border_pointer)?;

            let boxed = unsafe {
                (*border_pointer).hwnd = hwnd;
                Box::from_raw(border_pointer)
            };
            border_sender.send(boxed)?;

            let mut msg: MSG = MSG::default();

            loop {
                unsafe {
                    if !GetMessageW(&mut msg, None, 0, 0).as_bool() {
                        tracing::debug!("border window event processing thread shutdown");
                        break;
                    };
                    // TODO: error handling
                    let _ = TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }

            Ok(())
        });

        let mut border = border_receiver.recv()?;

        // I have literally no idea, apparently this is to get rid of the black pixels
        // around the edges of rounded corners? @lukeyou05 borrowed this from PowerToys
        unsafe {
            let pos: i32 = -GetSystemMetrics(SM_CXVIRTUALSCREEN) - 8;
            let hrgn = CreateRectRgn(pos, 0, pos + 1, 1);
            let mut bh: DWM_BLURBEHIND = Default::default();
            if !hrgn.is_invalid() {
                bh = DWM_BLURBEHIND {
                    dwFlags: DWM_BB_ENABLE | DWM_BB_BLURREGION,
                    fEnable: TRUE,
                    hRgnBlur: hrgn,
                    fTransitionOnMaximized: FALSE,
                };
            }

            let _ = DwmEnableBlurBehindWindow(border.hwnd(), &bh);
        }

        border.update_brushes()?;

        Ok(border)
    }

    pub fn update_brushes(&mut self) -> color_eyre::Result<()> {
        let hwnd_render_target_properties = D2D1_HWND_RENDER_TARGET_PROPERTIES {
            hwnd: HWND(windows_api::as_ptr!(self.hwnd)),
            pixelSize: Default::default(),
            presentOptions: D2D1_PRESENT_OPTIONS_IMMEDIATELY,
        };

        let render_target_properties = D2D1_RENDER_TARGET_PROPERTIES {
            r#type: D2D1_RENDER_TARGET_TYPE_DEFAULT,
            pixelFormat: D2D1_PIXEL_FORMAT {
                format: DXGI_FORMAT_UNKNOWN,
                alphaMode: D2D1_ALPHA_MODE_PREMULTIPLIED,
            },
            dpiX: 96.0,
            dpiY: 96.0,
            ..Default::default()
        };

        match unsafe {
            RENDER_FACTORY
                .CreateHwndRenderTarget(&render_target_properties, &hwnd_render_target_properties)
        } {
            Ok(render_target) => unsafe {
                self.brush_properties = *BRUSH_PROPERTIES.deref();
                for window_kind in [
                    WindowKind::Single,
                    WindowKind::Stack,
                    WindowKind::Monocle,
                    WindowKind::Unfocused,
                    WindowKind::Floating,
                    WindowKind::UnfocusedLocked,
                ] {
                    let color = window_kind_colour(window_kind);
                    let color = D2D1_COLOR_F {
                        r: ((color & 0xFF) as f32) / 255.0,
                        g: (((color >> 8) & 0xFF) as f32) / 255.0,
                        b: (((color >> 16) & 0xFF) as f32) / 255.0,
                        a: 1.0,
                    };

                    if let Ok(brush) =
                        render_target.CreateSolidColorBrush(&color, Some(&self.brush_properties))
                    {
                        self.brushes.insert(window_kind, brush);
                    }
                }

                render_target.SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                self.render_target = Some(RenderTarget(render_target));

                self.rounded_rect = {
                    let radius = 8.0 + self.width as f32 / 2.0;
                    D2D1_ROUNDED_RECT {
                        rect: Default::default(),
                        radiusX: radius,
                        radiusY: radius,
                    }
                };

                Ok(())
            },
            Err(error) => Err(error.into()),
        }
    }

    pub fn destroy(&self) -> color_eyre::Result<()> {
        // signal that we're destroying - prevents new render operations from starting
        self.is_destroying.store(true, Ordering::Release);

        // small delay to allow in-flight render operations to complete
        std::thread::sleep(std::time::Duration::from_millis(10));

        // WM_DESTROY will clear GWLP_USERDATA and drop the render target before D2D
        // frees its internal HwndPresenter during WM_NCDESTROY
        WindowsApi::close_window(self.hwnd)
    }

    /// Post a message to the border's own message loop thread requesting a brush update.
    /// This ensures update_brushes() always runs on the window thread that owns the D2D
    /// render target, preventing a data race with concurrent WndProc render operations.
    pub fn request_brush_update(&self) {
        let _ = unsafe {
            PostMessageW(
                Option::from(self.hwnd()),
                WM_UPDATE_BRUSHES,
                WPARAM(0),
                LPARAM(0),
            )
        };
    }

    pub fn set_position(&self, rect: &Rect, reference_hwnd: isize) -> color_eyre::Result<()> {
        let mut rect = *rect;
        rect.add_margin(self.width);
        rect.add_padding(-self.offset);

        WindowsApi::set_border_pos(self.hwnd, &rect, reference_hwnd)?;

        Ok(())
    }

    // this triggers WM_PAINT in the callback below
    pub fn invalidate(&self) {
        let _ = unsafe { InvalidateRect(Option::from(self.hwnd()), None, false) };
    }

    pub extern "system" fn callback(
        window: HWND,
        message: u32,
        wparam: WPARAM,
        lparam: LPARAM,
    ) -> LRESULT {
        unsafe {
            match message {
                WM_SETCURSOR => match LoadCursorW(None, IDC_ARROW) {
                    Ok(cursor) => {
                        SetCursor(Some(cursor));
                        LRESULT(0)
                    }
                    Err(error) => {
                        tracing::error!("{error}");
                        LRESULT(1)
                    }
                },
                WM_CREATE => {
                    let mut border_pointer: *mut Border =
                        GetWindowLongPtrW(window, GWLP_USERDATA) as _;

                    if border_pointer.is_null() {
                        let create_struct: *mut CREATESTRUCTW = lparam.0 as *mut _;
                        border_pointer = (*create_struct).lpCreateParams as *mut _;
                        SetWindowLongPtrW(window, GWLP_USERDATA, border_pointer as _);
                    }

                    LRESULT(0)
                }
                EVENT_OBJECT_DESTROY => {
                    let border_pointer: *mut Border = GetWindowLongPtrW(window, GWLP_USERDATA) as _;

                    if border_pointer.is_null() {
                        return LRESULT(0);
                    }

                    // we don't actually want to destroy the window here, just hide it for quicker
                    // visual feedback to the user; the actual destruction will be handled by the
                    // core border manager loop
                    WindowsApi::hide_window(window.0 as isize);
                    LRESULT(0)
                }
                EVENT_OBJECT_LOCATIONCHANGE => {
                    let border_pointer: *mut Border = GetWindowLongPtrW(window, GWLP_USERDATA) as _;

                    if border_pointer.is_null() {
                        return LRESULT(0);
                    }

                    if (*border_pointer).is_destroying.load(Ordering::Acquire) {
                        return LRESULT(0);
                    }

                    let reference_hwnd = (*border_pointer).tracking_hwnd;

                    let old_rect = (*border_pointer).window_rect;
                    let rect = WindowsApi::window_rect(reference_hwnd).unwrap_or_default();

                    (*border_pointer).window_rect = rect;

                    if let Err(error) = (*border_pointer).set_position(&rect, reference_hwnd) {
                        tracing::error!("failed to update border position {error}");
                    }

                    if (!rect.is_same_size_as(&old_rect) || !rect.has_same_position_as(&old_rect))
                        && let Some(render_target) = (*border_pointer).render_target.as_ref()
                    {
                        // double-check destruction flag before rendering
                        if (*border_pointer).is_destroying.load(Ordering::Acquire) {
                            return LRESULT(0);
                        }

                        let border_width = (*border_pointer).width;
                        let border_offset = (*border_pointer).offset;

                        (*border_pointer).rounded_rect.rect = D2D_RECT_F {
                            left: (border_width / 2 - border_offset) as f32,
                            top: (border_width / 2 - border_offset) as f32,
                            right: (rect.right - border_width / 2 + border_offset) as f32,
                            bottom: (rect.bottom - border_width / 2 + border_offset) as f32,
                        };

                        let _ = render_target.Resize(&D2D_SIZE_U {
                            width: rect.right as u32,
                            height: rect.bottom as u32,
                        });

                        let window_kind = (*border_pointer).window_kind;
                        let faded = faded_colour(&mut *border_pointer);
                        if let Some(brush) = (*border_pointer).brushes.get(&window_kind) {
                            // Aether: focus fade (the brush gets the in-between colour, then its own again)
                            let c = window_kind_colour(window_kind);
                            let (r, g, b) = colour_f(c);
                            brush.SetColor(&faded.unwrap_or(D2D1_COLOR_F { r, g, b, a: 1.0 }));
                            render_target.BeginDraw();
                            render_target.Clear(None);
                            // Aether: a gradient (spinning) border when hyprland.conf has one
                            let solid: windows::Win32::Graphics::Direct2D::ID2D1Brush = windows_core::Interface::cast(brush).unwrap();
                            let paint = gradient_paint(&**render_target, &*border_pointer, window_kind, faded.is_some(), rect.right as f32, rect.bottom as f32).unwrap_or(solid);

                            // Calculate border radius based on style
                            let style = match (*border_pointer).style {
                                BorderStyle::System => {
                                    if *WINDOWS_11 {
                                        BorderStyle::Rounded
                                    } else {
                                        BorderStyle::Square
                                    }
                                }
                                BorderStyle::Rounded => BorderStyle::Rounded,
                                BorderStyle::Square => BorderStyle::Square,
                            };

                            match style {
                                BorderStyle::Rounded => {
                                    render_target.DrawRoundedRectangle(
                                        &(*border_pointer).rounded_rect,
                                        &paint,
                                        border_width as f32,
                                        None,
                                    );
                                }
                                BorderStyle::Square => {
                                    render_target.DrawRectangle(
                                        &(*border_pointer).rounded_rect.rect,
                                        &paint,
                                        border_width as f32,
                                        None,
                                    );
                                }
                                _ => {}
                            }

                            let _ = render_target.EndDraw(None, None);
                        }
                    }

                    LRESULT(0)
                }
                WM_PAINT => {
                    if let Ok(rect) = WindowsApi::window_rect(window.0 as isize) {
                        let border_pointer: *mut Border =
                            GetWindowLongPtrW(window, GWLP_USERDATA) as _;

                        if border_pointer.is_null() {
                            return LRESULT(0);
                        }

                        if (*border_pointer).is_destroying.load(Ordering::Acquire) {
                            return LRESULT(0);
                        }

                        let reference_hwnd = (*border_pointer).tracking_hwnd;

                        // Update position to update the ZOrder
                        let border_window_rect = (*border_pointer).window_rect;

                        tracing::trace!("updating border position");
                        if let Err(error) =
                            (*border_pointer).set_position(&border_window_rect, reference_hwnd)
                        {
                            tracing::error!("failed to update border position {error}");
                        }

                        if let Some(render_target) = (*border_pointer).render_target.as_ref() {
                            // double-check destruction flag before rendering
                            if (*border_pointer).is_destroying.load(Ordering::Acquire) {
                                return LRESULT(0);
                            }

                            (*border_pointer).width = BORDER_WIDTH.load(Ordering::Relaxed);
                            (*border_pointer).offset = BORDER_OFFSET.load(Ordering::Relaxed);

                            let border_width = (*border_pointer).width;
                            let border_offset = (*border_pointer).offset;

                            (*border_pointer).rounded_rect.rect = D2D_RECT_F {
                                left: (border_width / 2 - border_offset) as f32,
                                top: (border_width / 2 - border_offset) as f32,
                                right: (rect.right - border_width / 2 + border_offset) as f32,
                                bottom: (rect.bottom - border_width / 2 + border_offset) as f32,
                            };

                            let _ = render_target.Resize(&D2D_SIZE_U {
                                width: rect.right as u32,
                                height: rect.bottom as u32,
                            });

                            // Get window kind and color
                            let window_kind = (*border_pointer).window_kind;
                            let faded = faded_colour(&mut *border_pointer);
                            if let Some(brush) = (*border_pointer).brushes.get(&window_kind) {
                                // Aether: focus fade
                                let (r, g, b) = colour_f(window_kind_colour(window_kind));
                                brush.SetColor(&faded.unwrap_or(D2D1_COLOR_F { r, g, b, a: 1.0 }));
                                render_target.BeginDraw();
                                render_target.Clear(None);
                                // Aether: a gradient (spinning) border when hyprland.conf has one
                                let solid: windows::Win32::Graphics::Direct2D::ID2D1Brush = windows_core::Interface::cast(brush).unwrap();
                                let paint = gradient_paint(&**render_target, &*border_pointer, window_kind, faded.is_some(), rect.right as f32, rect.bottom as f32).unwrap_or(solid);

                                (*border_pointer).style = STYLE.load();

                                // Calculate border radius based on style
                                let style = match (*border_pointer).style {
                                    BorderStyle::System => {
                                        if *WINDOWS_11 {
                                            BorderStyle::Rounded
                                        } else {
                                            BorderStyle::Square
                                        }
                                    }
                                    BorderStyle::Rounded => BorderStyle::Rounded,
                                    BorderStyle::Square => BorderStyle::Square,
                                };

                                match style {
                                    BorderStyle::Rounded => {
                                        render_target.DrawRoundedRectangle(
                                            &(*border_pointer).rounded_rect,
                                            &paint,
                                            border_width as f32,
                                            None,
                                        );
                                    }
                                    BorderStyle::Square => {
                                        render_target.DrawRectangle(
                                            &(*border_pointer).rounded_rect.rect,
                                            &paint,
                                            border_width as f32,
                                            None,
                                        );
                                    }
                                    _ => {}
                                }

                                let _ = render_target.EndDraw(None, None);
                            }
                        }
                    }
                    let _ = ValidateRect(Option::from(window), None);
                    LRESULT(0)
                }
                WM_UPDATE_BRUSHES => {
                    let border_pointer: *mut Border = GetWindowLongPtrW(window, GWLP_USERDATA) as _;
                    if border_pointer.is_null() {
                        return LRESULT(0);
                    }
                    if (*border_pointer).is_destroying.load(Ordering::Acquire) {
                        return LRESULT(0);
                    }
                    if let Err(error) = (*border_pointer).update_brushes() {
                        tracing::error!("failed to update brushes: {error}");
                    }
                    (*border_pointer).invalidate();
                    LRESULT(0)
                }
                WM_DESTROY => {
                    let border_pointer: *mut Border = GetWindowLongPtrW(window, GWLP_USERDATA) as _;
                    if !border_pointer.is_null() {
                        (*border_pointer).render_target = None;
                        (*border_pointer).brushes.clear();
                        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                    }
                    PostQuitMessage(0);
                    LRESULT(0)
                }
                _ => DefWindowProcW(window, message, wparam, lparam),
            }
        }
    }
}
