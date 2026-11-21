//! Hyprland's IPC: the request socket (`hyprctl`, `j/clients`, `dispatch ...`) and the event socket
//! (`workspace>>2`, `activewindow>>class,title`, ...), so hyprctl and every shell's Hyprland code talk to this
//! window manager directly.
//!
//! Clients find Hyprland at `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock` / `.socket2.sock`
//! through Qt's local sockets, which on Windows are named pipes named after that path - so the servers here are
//! named pipes with exactly that name (forward slashes: a pipe name cannot hold a backslash). Aether's launcher
//! sets both variables; without them: `%TEMP%` and the signature `aether`.
//!
//! Workspace ids: 1-10 on the first monitor, 11-20 on the second, ... (Hyprland's are global). A bare 1-10 in a
//! dispatcher still means "on the focused monitor" (binds like ALT+3); 11+ picks the monitor.

use std::sync::Arc;
use std::sync::LazyLock;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::Ordering;

use parking_lot::Mutex;
use serde_json::Value;
use serde_json::json;
use windows::Win32::Foundation::CloseHandle;
use windows::Win32::Foundation::HANDLE;
use windows::Win32::Foundation::INVALID_HANDLE_VALUE;
use windows::Win32::Storage::FileSystem::FlushFileBuffers;
use windows::Win32::Storage::FileSystem::PIPE_ACCESS_DUPLEX;
use windows::Win32::Storage::FileSystem::ReadFile;
use windows::Win32::Storage::FileSystem::WriteFile;
use windows::Win32::System::Pipes::ConnectNamedPipe;
use windows::Win32::System::Pipes::CreateNamedPipeW;
use windows::Win32::System::Pipes::DisconnectNamedPipe;
use windows::Win32::System::Pipes::PIPE_READMODE_BYTE;
use windows::Win32::System::Pipes::PIPE_TYPE_BYTE;
use windows::Win32::System::Pipes::PIPE_UNLIMITED_INSTANCES;
use windows::Win32::System::Pipes::PIPE_WAIT;
use windows::core::PCWSTR;

use crate::State;
use crate::window_manager::WindowManager;

/// one workspace id block per monitor
pub const IDS_PER_MONITOR: usize = 10;

pub fn workspace_id(monitor: usize, workspace: usize) -> i64 {
    (monitor * IDS_PER_MONITOR + workspace + 1) as i64
}

/// id -> (monitor, workspace); `None` for ids that are not ours
pub fn workspace_of(id: i64) -> Option<(usize, usize)> {
    if id < 1 {
        return None;
    }
    let id = id as usize - 1;
    Some((id / IDS_PER_MONITOR, id % IDS_PER_MONITOR))
}

// ------------------------------------------------------------------------------------------------ model

#[derive(Debug, Clone, PartialEq)]
pub struct Client {
    pub hwnd: i64,
    pub title: String,
    /// lower-case exe without `.exe` (what the shells map to app icons / desktop entries)
    pub class: String,
    pub at: (i64, i64),
    pub size: (i64, i64),
    pub monitor: usize,
    pub workspace: usize,
    pub floating: bool,
    pub fullscreen: bool,
}

impl Client {
    pub fn address(&self) -> String {
        format!("0x{:x}", self.hwnd)
    }
    /// event sockets use the address without 0x
    pub fn bare(&self) -> String {
        format!("{:x}", self.hwnd)
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Monitor {
    pub name: String,
    pub description: String,
    pub x: i64,
    pub y: i64,
    pub width: i64,
    pub height: i64,
    /// reserved space (left, top, right, bottom): bars / docks outside the work area
    pub reserved: [i64; 4],
    pub focused_workspace: usize,
    pub workspace_names: Vec<Option<String>>,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Model {
    pub monitors: Vec<Monitor>,
    pub clients: Vec<Client>,
    pub focused_monitor: usize,
    pub active: Option<i64>,
}

fn rect_of(v: &Value) -> (i64, i64, i64, i64) {
    let n = |k: &str| v.get(k).and_then(Value::as_i64).unwrap_or(0);
    (n("left"), n("top"), n("right"), n("bottom"))
}

fn class_of(exe: &str) -> String {
    let exe = exe.rsplit(['\\', '/']).next().unwrap_or(exe);
    exe.strip_suffix(".exe").or_else(|| exe.strip_suffix(".EXE")).unwrap_or(exe).to_ascii_lowercase()
}

fn ring(v: &Value) -> (&[Value], usize) {
    let elements = v.get("elements").and_then(Value::as_array).map(Vec::as_slice).unwrap_or(&[]);
    let focused = v.get("focused").and_then(Value::as_u64).unwrap_or(0) as usize;
    (elements, focused)
}

impl Model {
    /// from komorebi's State (serialised)
    pub fn from_state(state: &Value) -> Model {
        let mut model = Model::default();
        let (monitors, focused_monitor) = ring(&state["monitors"]);
        model.focused_monitor = focused_monitor;
        for (m, monitor) in monitors.iter().enumerate() {
            let (x, y, w, h) = rect_of(&monitor["size"]);
            let (wl, wt, ww, wh) = rect_of(&monitor["work_area_size"]);
            let (workspaces, focused_ws) = ring(&monitor["workspaces"]);
            model.monitors.push(Monitor {
                name: monitor["name"].as_str().unwrap_or("").to_string(),
                description: monitor["device"].as_str().unwrap_or("").to_string(),
                x,
                y,
                width: w,
                height: h,
                reserved: [wl - x, wt - y, (x + w) - (wl + ww), (y + h) - (wt + wh)],
                focused_workspace: focused_ws,
                workspace_names: workspaces.iter().map(|ws| ws["name"].as_str().map(str::to_string)).collect(),
            });
            for (w_idx, ws) in workspaces.iter().enumerate() {
                let push = |win: &Value, floating: bool, fullscreen: bool, model: &mut Model| {
                    let Some(hwnd) = win["hwnd"].as_i64() else { return };
                    let (l, t, r, b) = rect_of(&win["rect"]);
                    model.clients.push(Client {
                        hwnd,
                        title: win["title"].as_str().unwrap_or("").to_string(),
                        class: class_of(win["exe"].as_str().unwrap_or("")),
                        at: (l, t),
                        size: (r, b),
                        monitor: m,
                        workspace: w_idx,
                        floating,
                        fullscreen,
                    });
                };
                let (containers, focused_c) = ring(&ws["containers"]);
                for container in containers {
                    for win in ring(&container["windows"]).0 {
                        push(win, false, false, &mut model);
                    }
                }
                if let Some(monocle) = ws.get("monocle_container").filter(|v| !v.is_null()) {
                    for win in ring(&monocle["windows"]).0 {
                        push(win, false, true, &mut model);
                    }
                }
                if let Some(max) = ws.get("maximized_window").filter(|v| !v.is_null()) {
                    push(max, false, true, &mut model);
                }
                let floating = &ws["floating_windows"];
                let floats: Vec<Value> = floating.as_array().cloned().unwrap_or_else(|| ring(floating).0.to_vec());
                for win in &floats {
                    push(win, true, false, &mut model);
                }
                // the active window: the focused container's focused window on the focused workspace
                if m == focused_monitor && w_idx == focused_ws {
                    let active = containers.get(focused_c).and_then(|c| {
                        let (wins, f) = ring(&c["windows"]);
                        wins.get(f).and_then(|w| w["hwnd"].as_i64())
                    });
                    model.active = active.or_else(|| ws.get("monocle_container").and_then(|c| ring(&c["windows"]).0.first().and_then(|w| w["hwnd"].as_i64())));
                }
            }
        }
        // the shown canvas: its windows float on the workspace it is shown on (bars list them there); a hidden
        // canvas is not listed, like a hidden special workspace
        if let (hwnds, Some((m, w))) = super::special::canvas_snapshot() {
            for hwnd in hwnds {
                if model.clients.iter().any(|c| c.hwnd == hwnd as i64) {
                    continue;
                }
                let Ok(rect) = crate::windows_api::WindowsApi::window_rect(hwnd) else { continue };
                let exe = crate::Window::from(hwnd).exe().unwrap_or_default();
                model.clients.push(Client {
                    hwnd: hwnd as i64,
                    title: crate::windows_api::WindowsApi::window_text_w(hwnd).unwrap_or_default(),
                    class: class_of(&exe),
                    at: (rect.left as i64, rect.top as i64),
                    size: (rect.right as i64, rect.bottom as i64),
                    monitor: m,
                    workspace: w,
                    floating: true,
                    fullscreen: false,
                });
            }
        }
        // a focused floating window wins (komorebi keeps tiled focus separately)
        if let Ok(foreground) = crate::windows_api::WindowsApi::foreground_window()
            && model.clients.iter().any(|c| c.hwnd == foreground as i64)
        {
            model.active = Some(foreground as i64);
        }
        model
    }

    pub fn ws_name(&self, monitor: usize, workspace: usize) -> String {
        self.monitors
            .get(monitor)
            .and_then(|m| m.workspace_names.get(workspace).cloned().flatten())
            .unwrap_or_else(|| workspace_id(monitor, workspace).to_string())
    }

    fn client(&self, hwnd: i64) -> Option<&Client> {
        self.clients.iter().find(|c| c.hwnd == hwnd)
    }

    /// Hyprland lists workspaces that exist: with windows, or shown on a monitor
    pub fn live_workspaces(&self) -> Vec<(usize, usize)> {
        let mut out = Vec::new();
        for (m, monitor) in self.monitors.iter().enumerate() {
            for w in 0..monitor.workspace_names.len() {
                if w == monitor.focused_workspace || self.clients.iter().any(|c| c.monitor == m && c.workspace == w) {
                    out.push((m, w));
                }
            }
        }
        out
    }

    // ---------------------------------------------------------------------------------------- JSON

    fn ws_ref(&self, m: usize, w: usize) -> Value {
        json!({ "id": workspace_id(m, w), "name": self.ws_name(m, w) })
    }

    pub fn client_json(&self, c: &Client) -> Value {
        let pid = {
            let (pid, _) = crate::windows_api::WindowsApi::window_thread_process_id(c.hwnd as isize);
            pid
        };
        json!({
            "address": c.address(),
            "mapped": true,
            "hidden": self.monitors.get(c.monitor).is_some_and(|m| m.focused_workspace != c.workspace),
            "at": [c.at.0, c.at.1],
            "size": [c.size.0, c.size.1],
            "workspace": self.ws_ref(c.monitor, c.workspace),
            "floating": c.floating,
            "pseudo": false,
            "monitor": c.monitor,
            "class": c.class,
            "title": c.title,
            "initialClass": c.class,
            "initialTitle": c.title,
            "pid": pid,
            "xwayland": false,
            "pinned": false,
            "fullscreen": if c.fullscreen { 1 } else { 0 },
            "fullscreenClient": if c.fullscreen { 1 } else { 0 },
            "grouped": [],
            "tags": [],
            "swallowing": "0x0",
            "focusHistoryID": if Some(c.hwnd) == self.active { 0 } else { 1 },
            "inhibitingIdle": false,
            "xdgTag": "",
            "xdgDescription": "",
            "contentType": "none"
        })
    }

    pub fn clients_json(&self) -> Value {
        Value::Array(self.clients.iter().map(|c| self.client_json(c)).collect())
    }

    pub fn workspace_json(&self, m: usize, w: usize) -> Value {
        let windows: Vec<&Client> = self.clients.iter().filter(|c| c.monitor == m && c.workspace == w).collect();
        let last = windows.iter().find(|c| Some(c.hwnd) == self.active).or(windows.first());
        json!({
            "id": workspace_id(m, w),
            "name": self.ws_name(m, w),
            "monitor": self.monitors.get(m).map(|x| x.name.clone()).unwrap_or_default(),
            "monitorID": m,
            "windows": windows.len(),
            "hasfullscreen": windows.iter().any(|c| c.fullscreen),
            "lastwindow": last.map(|c| c.address()).unwrap_or_else(|| "0x0".into()),
            "lastwindowtitle": last.map(|c| c.title.clone()).unwrap_or_default(),
            "ispersistent": true
        })
    }

    pub fn workspaces_json(&self) -> Value {
        Value::Array(self.live_workspaces().into_iter().map(|(m, w)| self.workspace_json(m, w)).collect())
    }

    pub fn monitors_json(&self) -> Value {
        Value::Array(
            self.monitors
                .iter()
                .enumerate()
                .map(|(m, mon)| {
                    json!({
                        "id": m,
                        "name": mon.name,
                        "description": mon.description,
                        "make": "",
                        "model": mon.description,
                        "serial": "",
                        "width": mon.width,
                        "height": mon.height,
                        "physicalWidth": 0,
                        "physicalHeight": 0,
                        "refreshRate": 60.0,
                        "x": mon.x,
                        "y": mon.y,
                        "activeWorkspace": self.ws_ref(m, mon.focused_workspace),
                        "specialWorkspace": { "id": 0, "name": "" },
                        "reserved": mon.reserved,
                        "scale": 1.0,
                        "transform": 0,
                        "focused": m == self.focused_monitor,
                        "dpmsStatus": true,
                        "vrr": false,
                        "solitary": "0",
                        "solitaryBlockedBy": [],
                        "activelyTearing": false,
                        "tearingBlockedBy": [],
                        "directScanoutTo": "0",
                        "directScanoutBlockedBy": [],
                        "disabled": false,
                        "currentFormat": "XRGB8888",
                        "mirrorOf": "none",
                        "availableModes": [format!("{}x{}@60.00Hz", mon.width, mon.height)]
                    })
                })
                .collect(),
        )
    }

    pub fn active_window_json(&self) -> Value {
        self.active.and_then(|h| self.client(h)).map_or_else(|| json!({}), |c| self.client_json(c))
    }

    pub fn active_workspace_json(&self) -> Value {
        let m = self.focused_monitor;
        let w = self.monitors.get(m).map_or(0, |x| x.focused_workspace);
        self.workspace_json(m, w)
    }
}

// --------------------------------------------------------------------------------------------- events

/// the Hyprland events between two models (what changed), in Hyprland's order
pub fn events_between(old: &Model, new: &Model) -> Vec<String> {
    let mut out = Vec::new();
    let old_live: Vec<(usize, usize)> = old.live_workspaces();
    let new_live: Vec<(usize, usize)> = new.live_workspaces();
    for &(m, w) in &new_live {
        if !old_live.contains(&(m, w)) {
            out.push(format!("createworkspace>>{}", new.ws_name(m, w)));
            out.push(format!("createworkspacev2>>{},{}", workspace_id(m, w), new.ws_name(m, w)));
        }
    }
    // windows that appeared / moved / changed
    for c in &new.clients {
        match old.client(c.hwnd) {
            None => out.push(format!("openwindow>>{},{},{},{}", c.bare(), new.ws_name(c.monitor, c.workspace), c.class, c.title)),
            Some(o) => {
                if (o.monitor, o.workspace) != (c.monitor, c.workspace) {
                    out.push(format!("movewindow>>{},{}", c.bare(), new.ws_name(c.monitor, c.workspace)));
                    out.push(format!("movewindowv2>>{},{},{}", c.bare(), workspace_id(c.monitor, c.workspace), new.ws_name(c.monitor, c.workspace)));
                }
                if o.title != c.title {
                    out.push(format!("windowtitle>>{}", c.bare()));
                    out.push(format!("windowtitlev2>>{},{}", c.bare(), c.title));
                }
                if o.floating != c.floating {
                    out.push(format!("changefloatingmode>>{},{}", c.bare(), u8::from(c.floating)));
                }
            }
        }
    }
    for c in &old.clients {
        if new.client(c.hwnd).is_none() {
            out.push(format!("closewindow>>{}", c.bare()));
        }
    }
    // monitor / workspace focus
    let nm = new.focused_monitor;
    let new_ws = new.monitors.get(nm).map(|m| m.focused_workspace);
    let old_ws = old.monitors.get(nm).map(|m| m.focused_workspace);
    if old.focused_monitor != nm {
        if let (Some(mon), Some(w)) = (new.monitors.get(nm), new_ws) {
            out.push(format!("focusedmon>>{},{}", mon.name, new.ws_name(nm, w)));
            out.push(format!("focusedmonv2>>{},{}", mon.name, workspace_id(nm, w)));
        }
    }
    if (old.focused_monitor != nm || old_ws != new_ws)
        && let Some(w) = new_ws
    {
        out.push(format!("workspace>>{}", new.ws_name(nm, w)));
        out.push(format!("workspacev2>>{},{}", workspace_id(nm, w), new.ws_name(nm, w)));
    }
    // other monitors switching workspace (no focus change): Hyprland still says which workspace is shown
    for (m, mon) in new.monitors.iter().enumerate() {
        if m == nm {
            continue;
        }
        if old.monitors.get(m).map(|o| o.focused_workspace) != Some(mon.focused_workspace) {
            out.push(format!("workspacev2>>{},{}", workspace_id(m, mon.focused_workspace), new.ws_name(m, mon.focused_workspace)));
        }
    }
    let active_changed = old.active != new.active
        || new.active.and_then(|h| new.client(h)).map(|c| &c.title) != old.active.and_then(|h| old.client(h)).map(|c| &c.title);
    if active_changed {
        match new.active.and_then(|h| new.client(h)) {
            Some(c) => {
                out.push(format!("activewindow>>{},{}", c.class, c.title));
                out.push(format!("activewindowv2>>{}", c.bare()));
            }
            None => {
                out.push("activewindow>>,".into());
                out.push("activewindowv2>>".into());
            }
        }
    }
    let new_fs = new.active.and_then(|h| new.client(h)).is_some_and(|c| c.fullscreen);
    let old_fs = old.active.and_then(|h| old.client(h)).is_some_and(|c| c.fullscreen);
    if new_fs != old_fs {
        out.push(format!("fullscreen>>{}", u8::from(new_fs)));
    }
    for &(m, w) in &old_live {
        if !new_live.contains(&(m, w)) {
            out.push(format!("destroyworkspace>>{}", old.ws_name(m, w)));
            out.push(format!("destroyworkspacev2>>{},{}", workspace_id(m, w), old.ws_name(m, w)));
        }
    }
    out
}

// -------------------------------------------------------------------------------------------- sockets

static STARTED: AtomicBool = AtomicBool::new(false);
static LAST: LazyLock<Mutex<Option<Model>>> = LazyLock::new(|| Mutex::new(None));
static WM: LazyLock<Mutex<Option<Arc<Mutex<WindowManager>>>>> = LazyLock::new(|| Mutex::new(None));
/// event-socket clients (pipe handles as isize: HANDLE is not Send)
static LISTENERS: LazyLock<Mutex<Vec<isize>>> = LazyLock::new(|| Mutex::new(Vec::new()));

/// `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE` with forward slashes (sets both for children)
pub fn socket_dir() -> String {
    let runtime = std::env::var("XDG_RUNTIME_DIR")
        .ok()
        .filter(|v| !v.is_empty())
        .unwrap_or_else(|| std::env::temp_dir().to_string_lossy().trim_end_matches(['\\', '/']).to_string())
        .replace('\\', "/");
    let signature = std::env::var("HYPRLAND_INSTANCE_SIGNATURE").ok().filter(|v| !v.is_empty()).unwrap_or_else(|| "aether".into());
    // what `exec`ed programs (hyprctl in scripts, shells started from binds) need to find us
    unsafe {
        std::env::set_var("XDG_RUNTIME_DIR", &runtime);
        std::env::set_var("HYPRLAND_INSTANCE_SIGNATURE", &signature);
    }
    let dir = format!("{runtime}/hypr/{signature}");
    // clients check that the folder exists before they connect to the pipes (Caelestia's HyprExtras does)
    let _ = std::fs::create_dir_all(&dir);
    dir
}

fn pipe_name(path: &str) -> Vec<u16> {
    format!(r"\\.\pipe\{path}").encode_utf16().chain(Some(0)).collect()
}

fn create_pipe(name: &[u16]) -> Option<HANDLE> {
    let handle = unsafe {
        CreateNamedPipeW(
            PCWSTR(name.as_ptr()),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64 * 1024,
            64 * 1024,
            0,
            None,
        )
    };
    (handle != INVALID_HANDLE_VALUE && !handle.is_invalid()).then_some(handle)
}

fn write_all(handle: HANDLE, bytes: &[u8]) -> bool {
    let mut offset = 0;
    while offset < bytes.len() {
        let mut written = 0u32;
        if unsafe { WriteFile(handle, Some(&bytes[offset..]), Some(&mut written), None) }.is_err() || written == 0 {
            return false;
        }
        offset += written as usize;
    }
    true
}

fn serve(name: Vec<u16>, on_client: fn(isize)) {
    std::thread::spawn(move || {
        loop {
            let Some(pipe) = create_pipe(&name) else {
                tracing::error!("hypr ipc: could not create pipe {}", String::from_utf16_lossy(&name));
                std::thread::sleep(std::time::Duration::from_secs(2));
                continue;
            };
            // ERROR_PIPE_CONNECTED (a client that came in between) also counts as connected
            let _ = unsafe { ConnectNamedPipe(pipe, None) };
            let raw = pipe.0 as isize;
            std::thread::spawn(move || on_client(raw));
        }
    });
}

fn close(raw: isize) {
    let handle = HANDLE(raw as _);
    unsafe {
        let _ = FlushFileBuffers(handle);
        let _ = DisconnectNamedPipe(handle);
        let _ = CloseHandle(handle);
    }
}

fn on_request_client(raw: isize) {
    let handle = HANDLE(raw as _);
    let mut buffer = vec![0u8; 64 * 1024];
    let mut read = 0u32;
    if unsafe { ReadFile(handle, Some(&mut buffer), Some(&mut read), None) }.is_ok() && read > 0 {
        let request = String::from_utf8_lossy(&buffer[..read as usize]).to_string();
        let reply = handle_request(&request);
        let _ = write_all(handle, reply.as_bytes());
    }
    close(raw);
}

fn on_event_client(raw: isize) {
    tracing::info!("hypr ipc: event listener connected");
    LISTENERS.lock().push(raw);
}

fn broadcast(lines: &[String]) {
    if lines.is_empty() {
        return;
    }
    let text: String = lines.iter().map(|l| format!("{l}\n")).collect();
    let mut listeners = LISTENERS.lock();
    listeners.retain(|raw| {
        let ok = write_all(HANDLE(*raw as _), text.as_bytes());
        if !ok {
            close(*raw);
        }
        ok
    });
}

/// a submap change (binds.rs) as Hyprland's `submap>>name`
/// Aether: `aether <message>` binds reach the shells as the event "aether>>message" (no process started)
pub fn aether_event(message: &str) {
    broadcast(&[format!("aether>>{}", message.trim())]);
}

pub fn submap_changed(name: Option<&str>) {
    broadcast(&[format!("submap>>{}", name.unwrap_or(""))]);
}

fn current_model() -> Model {
    let wm = WM.lock().clone();
    match wm {
        Some(wm) => {
            let state = { State::from(&*wm.lock()) };
            Model::from_state(&serde_json::to_value(&state).unwrap_or_default())
        }
        None => LAST.lock().clone().unwrap_or_default(),
    }
}

/// komorebi's state changed (notify_subscribers): send the Hyprland events for what changed
pub fn on_state(state: &State) {
    if !STARTED.load(Ordering::SeqCst) {
        return;
    }
    let model = Model::from_state(&serde_json::to_value(state).unwrap_or_default());
    let mut last = LAST.lock();
    if let Some(old) = last.as_ref() {
        for client in &model.clients {
            if !old.clients.iter().any(|c| c.hwnd == client.hwnd) {
                super::rules::window_opened(client.hwnd as isize);
                super::looks::window_opened(client.hwnd as isize);
            }
        }
    }
    if let Some(monitor) = model.monitors.get(model.focused_monitor) {
        let (m, w) = (model.focused_monitor, monitor.focused_workspace);
        let empty = !model.clients.iter().any(|c| c.monitor == m && c.workspace == w);
        super::rules::workspace_shown(workspace_id(m, w), empty);
        super::special::canvas_workspace_shown(m, w);
    }
    let events = last.as_ref().map(|old| events_between(old, &model)).unwrap_or_default();
    *last = Some(model);
    drop(last);
    broadcast(&events);
}

pub fn start(wm: Arc<Mutex<WindowManager>>) {
    if STARTED.swap(true, Ordering::SeqCst) {
        return;
    }
    *WM.lock() = Some(wm);
    *LAST.lock() = Some(current_model());
    let dir = socket_dir();
    tracing::info!("hypr ipc: listening on {dir}/.socket.sock and .socket2.sock (named pipes)");
    serve(pipe_name(&format!("{dir}/.socket.sock")), on_request_client);
    serve(pipe_name(&format!("{dir}/.socket2.sock")), on_event_client);
}

// ------------------------------------------------------------------------------------------- requests

/// one request as Hyprland answers it (`j/clients`, `dispatch workspace 2`, `[[BATCH]]a;b`)
pub fn handle_request(request: &str) -> String {
    let request = request.trim_end_matches(['\0', '\n', '\r']).trim();
    if let Some(batch) = request.strip_prefix("[[BATCH]]") {
        return batch.split(';').map(|r| handle_request(r.trim())).collect::<Vec<_>>().join("\n\n");
    }
    // flags before the '/': j = json (others ignored: r, a, c)
    let (flags, command) = match request.split_once('/') {
        Some((f, rest)) if f.len() <= 4 && f.chars().all(|c| c.is_ascii_alphabetic()) => (f, rest),
        _ => ("", request),
    };
    let json = flags.contains('j');
    let (name, args) = command.split_once(' ').map_or((command, ""), |(n, a)| (n, a.trim()));
    let model = || current_model();
    let out = |v: Value, text: String| if json { serde_json::to_string_pretty(&v).unwrap_or_default() } else { text };
    match name {
        "clients" => {
            let m = model();
            let text = m.clients.iter().map(|c| format!("Window {} -> {}:\n\tworkspace: {} ({})\n\tclass: {}\n\ttitle: {}\n\tfloating: {}\n", c.bare(), c.title, workspace_id(c.monitor, c.workspace), m.ws_name(c.monitor, c.workspace), c.class, c.title, u8::from(c.floating))).collect();
            out(m.clients_json(), text)
        }
        "workspaces" => {
            let m = model();
            let text = m.live_workspaces().iter().map(|&(mo, w)| format!("workspace ID {} ({}) on monitor {}:\n\twindows: {}\n", workspace_id(mo, w), m.ws_name(mo, w), m.monitors[mo].name, m.clients.iter().filter(|c| c.monitor == mo && c.workspace == w).count())).collect();
            out(m.workspaces_json(), text)
        }
        "monitors" => {
            let m = model();
            let text = m.monitors.iter().enumerate().map(|(i, x)| format!("Monitor {} (ID {}):\n\t{}x{} at {}x{}\n\tactive workspace: {}\n", x.name, i, x.width, x.height, x.x, x.y, workspace_id(i, x.focused_workspace))).collect();
            out(m.monitors_json(), text)
        }
        "activewindow" => {
            let m = model();
            let text = m.active.and_then(|h| m.client(h)).map(|c| format!("Window {} -> {}:\n\tclass: {}\n", c.bare(), c.title, c.class)).unwrap_or_else(|| "Invalid".into());
            out(m.active_window_json(), text)
        }
        "activeworkspace" => {
            let m = model();
            let w = m.monitors.get(m.focused_monitor).map_or(0, |x| x.focused_workspace);
            out(m.active_workspace_json(), format!("workspace ID {} ({})\n", workspace_id(m.focused_monitor, w), m.ws_name(m.focused_monitor, w)))
        }
        "dispatch" => {
            let (dispatcher, arg) = args.split_once(' ').map_or((args, ""), |(d, a)| (d, a.trim()));
            match WM.lock().clone() {
                Some(wm) => {
                    run_dispatch(&wm, dispatcher, arg);
                    "ok".into()
                }
                None => "no window manager".into(),
            }
        }
        "getoption" => {
            let conf = super::load_config();
            match conf.get(args) {
                Some(v) => out(json!({ "option": args, "str": v, "int": v.trim().parse::<i64>().unwrap_or(0), "float": v.trim().parse::<f64>().unwrap_or(0.0), "set": true }), format!("str: {v}\nset: true\n")),
                None => out(json!({ "option": args, "set": false }), "no such option\n".into()),
            }
        }
        "binds" => {
            let conf = super::load_config();
            let list: Vec<Value> = conf
                .binds
                .iter()
                .map(|b| {
                    let mut mask = 0;
                    if b.mods.shift { mask |= 1 }
                    if b.mods.ctrl { mask |= 4 }
                    if b.mods.alt { mask |= 8 }
                    if b.mods.sup { mask |= 64 }
                    json!({ "locked": b.flags.locked, "mouse": b.flags.mouse, "release": b.flags.release, "repeat": b.flags.repeat,
                            "non_consuming": b.flags.non_consuming, "has_description": b.description.is_some(), "modmask": mask,
                            "submap": b.submap.clone().unwrap_or_default(), "key": b.key, "keycode": 0, "catch_all": false,
                            "description": b.description.clone().unwrap_or_default(), "dispatcher": b.dispatcher, "arg": b.arg })
                })
                .collect();
            out(Value::Array(list), format!("{} binds\n", conf.binds.len()))
        }
        "version" => out(
            json!({ "branch": "aether", "commit": "komorebi-fork", "version": "0.50.0", "dirty": false, "commit_message": "Hyprland on Windows (Aether)", "commit_date": "", "tag": "v0.50.0", "commits": "0", "buildAquamarine": "", "flags": [] }),
            "AetherWM (Aether's window manager, based on komorebi) - Hyprland IPC v0.50 compatible\n".into(),
        ),
        "reload" => {
            if let Some(wm) = WM.lock().clone() {
                std::thread::spawn(move || super::reload(&wm));
            }
            "ok".into()
        }
        "keyword" => {
            // a keyword is a one-line config: the same path as hyprland.conf (gaps, borders, colours, layout, binds)
            let (key, value) = args.split_once(char::is_whitespace).map_or((args, ""), |(k, v)| (k.trim(), v.trim()));
            let conf = super::conf::HyprConf::parse_str(&format!("{key} = {value}"), &super::config_path());
            if key == "animations:enabled" {
                *super::looks::ANIM_OVERRIDE.lock().unwrap() = conf.get_bool(key);
            }
            if !conf.window_rules.is_empty() {
                super::rules::add_rules(&conf.window_rules);
                return "ok".into();
            }
            if !conf.binds.is_empty() {
                let mut all: Vec<super::conf::Bind> = super::binds::current_binds();
                all.extend(conf.binds.iter().cloned());
                super::binds::set_binds(&all);
                return "ok".into();
            }
            match WM.lock().clone() {
                Some(wm) => {
                    let workspaces: Vec<(usize, usize)> = {
                        let wm = wm.lock();
                        wm.monitors().iter().enumerate().flat_map(|(m, mon)| (0..mon.workspaces().len()).map(move |w| (m, w))).collect()
                    };
                    let messages = super::apply_messages(&conf, &workspaces);
                    let layout_keyword = key.starts_with("general:layout") || key.starts_with("master:") || key.starts_with("dwindle:");
                    if messages.is_empty() && !layout_keyword && !key.starts_with("animation") && !key.starts_with("bezier") && !key.starts_with("decoration:") {
                        tracing::info!("hypr ipc: keyword {key} is not applied at runtime yet");
                    }
                    for message in messages {
                        let _ = crate::process_command::run_command(&wm, message);
                    }
                    if layout_keyword {
                        // master:/dwindle: options need the layout too: take it from the file unless this sets it
                        let mut full = super::load_config();
                        for (k, v) in &conf.options {
                            full.options.insert(k.clone(), v.clone());
                        }
                        super::apply_layouts(&wm, &full);
                    }
                    // animations / beziers / decoration: the looks again, with this keyword on top of the file
                    if key.starts_with("animation") || key.starts_with("bezier") || key.starts_with("decoration:") || key.starts_with("general:col") || key.starts_with("aether:") {
                        let mut full = super::load_config();
                        for (k, v) in &conf.options {
                            full.options.insert(k.clone(), v.clone());
                        }
                        full.beziers.extend(conf.beziers.iter().cloned());
                        full.animations.extend(conf.animations.iter().cloned());
                        super::looks::apply(&wm, &full);
                    }
                    "ok".into()
                }
                None => "no window manager".into(),
            }
        }
        "descriptions" => out(json!([]), String::new()),
        "layers" => out(json!({}), String::new()),
        "devices" => out(json!({ "mice": [], "keyboards": [], "tablets": [], "touch": [], "switches": [] }), String::new()),
        "workspacerules" | "globalshortcuts" | "animations" | "configerrors" => out(json!([]), String::new()),
        "splash" => "Hyprland on Windows".into(),
        "cursorpos" => {
            let p = crate::windows_api::WindowsApi::cursor_pos().unwrap_or_default();
            out(json!({ "x": p.x, "y": p.y }), format!("{}, {}", p.x, p.y))
        }
        "instances" => out(json!([{ "instance": "aether", "time": 0, "pid": std::process::id(), "wl_socket": "" }]), "instance aether\n".into()),
        "submap" => super::binds::current_submap().unwrap_or_else(|| "default".into()),
        "" => "unknown request".into(),
        other => format!("unknown request: {other}"),
    }
}

/// dispatchers from the IPC: workspace ids 11+ name a monitor (1-10 stay "on the focused monitor")
pub fn run_dispatch(wm: &Arc<Mutex<WindowManager>>, dispatcher: &str, arg: &str) {
    if matches!(dispatcher, "workspace" | "movetoworkspace" | "movetoworkspacesilent")
        && let Ok(id) = arg.split(',').next().unwrap_or("").trim().parse::<i64>()
        && id > IDS_PER_MONITOR as i64
        && let Some((m, w)) = workspace_of(id)
    {
        use crate::core::SocketMessage;
        let message = match dispatcher {
            "workspace" => SocketMessage::FocusMonitorWorkspaceNumber(m, w),
            "movetoworkspace" => SocketMessage::MoveContainerToMonitorWorkspaceNumber(m, w),
            _ => SocketMessage::SendContainerToMonitorWorkspaceNumber(m, w),
        };
        let _ = crate::process_command::run_command(wm, message);
        return;
    }
    super::binds::run(wm, dispatcher, arg);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn state() -> Value {
        json!({ "monitors": { "focused": 1, "elements": [
            { "name": "DISPLAY1", "device": "SAM", "size": {"left": -1920, "top": 0, "right": 1920, "bottom": 1080},
              "work_area_size": {"left": -1920, "top": 40, "right": 1920, "bottom": 1040},
              "workspaces": { "focused": 0, "elements": [ { "name": null, "containers": {"focused": 0, "elements": []}, "floating_windows": [] }, { "name": "web", "containers": {"focused": 0, "elements": []}, "floating_windows": [] } ] } },
            { "name": "DISPLAY5", "device": "HGC", "size": {"left": 0, "top": 0, "right": 1920, "bottom": 1080},
              "work_area_size": {"left": 61, "top": 42, "right": 1851, "bottom": 1030},
              "workspaces": { "focused": 1, "elements": [
                 { "name": null, "containers": {"focused": 0, "elements": [ { "windows": {"focused": 0, "elements": [ {"hwnd": 100, "title": "Discord", "exe": "Discord.exe", "rect": {"left": 0, "top": 0, "right": 10, "bottom": 10}} ]} } ]}, "floating_windows": [] },
                 { "name": null, "containers": {"focused": 1, "elements": [
                     { "windows": {"focused": 0, "elements": [ {"hwnd": 200, "title": "a.txt - Notepad", "exe": "C:\\Windows\\notepad.exe", "rect": {"left": 61, "top": 42, "right": 900, "bottom": 988}} ]} },
                     { "windows": {"focused": 0, "elements": [ {"hwnd": 255, "title": "Zen", "exe": "zen.exe", "rect": {"left": 961, "top": 42, "right": 900, "bottom": 988}} ]} } ]},
                   "floating_windows": { "elements": [ {"hwnd": 300, "title": "Calculator", "exe": "CalculatorApp.exe", "rect": {"left": 5, "top": 5, "right": 300, "bottom": 400}} ], "focused": 0 } } ] } } ] } })
    }

    #[test]
    fn ids() {
        assert_eq!(workspace_id(0, 0), 1);
        assert_eq!(workspace_id(1, 4), 15);
        assert_eq!(workspace_of(15), Some((1, 4)));
        assert_eq!(workspace_of(3), Some((0, 2)));
        assert_eq!(workspace_of(0), None);
    }

    #[test]
    fn model_from_state() {
        let m = Model::from_state(&state());
        assert_eq!(m.monitors.len(), 2);
        assert_eq!(m.focused_monitor, 1);
        assert_eq!(m.monitors[1].reserved, [61, 42, 8, 8]);
        assert_eq!(m.clients.len(), 4);
        let calc = m.clients.iter().find(|c| c.hwnd == 300).unwrap();
        assert!(calc.floating);
        assert_eq!(calc.class, "calculatorapp");
        assert_eq!(m.clients.iter().find(|c| c.hwnd == 200).unwrap().class, "notepad");
        // focused container 1 on the focused workspace (unless a client of ours is in the foreground)
        assert!(m.active == Some(255) || m.clients.iter().any(|c| Some(c.hwnd) == m.active));
        assert_eq!(m.ws_name(0, 1), "web");
        assert_eq!(m.ws_name(1, 1), "12");
        // live: focused ones + ones with windows
        assert_eq!(m.live_workspaces(), vec![(0, 0), (1, 0), (1, 1)]);
        let ws = m.workspaces_json();
        assert_eq!(ws.as_array().unwrap().len(), 3);
        let mon = m.monitors_json();
        assert_eq!(mon[1]["activeWorkspace"]["id"], 12);
        assert_eq!(mon[1]["focused"], true);
        let clients = m.clients_json();
        assert_eq!(clients[1]["address"], "0xc8");
        assert_eq!(clients[1]["workspace"]["id"], 12);
    }

    #[test]
    fn events() {
        let old = Model::from_state(&state());
        let mut s = state();
        // switch monitor 1 to workspace 1 (index 0), close Zen, open a new window there, retitle Discord
        s["monitors"]["elements"][1]["workspaces"]["focused"] = json!(0);
        let ws1 = &mut s["monitors"]["elements"][1]["workspaces"]["elements"][1]["containers"]["elements"];
        ws1.as_array_mut().unwrap().remove(1);
        s["monitors"]["elements"][1]["workspaces"]["elements"][0]["containers"]["elements"][0]["windows"]["elements"][0]["title"] = json!("Discord - #general");
        s["monitors"]["elements"][1]["workspaces"]["elements"][0]["containers"]["elements"].as_array_mut().unwrap().push(
            json!({ "windows": {"focused": 0, "elements": [ {"hwnd": 400, "title": "New", "exe": "wt.exe", "rect": {"left": 0, "top": 0, "right": 5, "bottom": 5}} ]} }));
        let new = Model::from_state(&s);
        let e = events_between(&old, &new);
        assert!(e.contains(&"openwindow>>190,11,wt,New".to_string()), "{e:?}");
        assert!(e.contains(&"closewindow>>ff".to_string()), "{e:?}");
        assert!(e.contains(&"windowtitlev2>>64,Discord - #general".to_string()), "{e:?}");
        assert!(e.contains(&"workspace>>11".to_string()), "{e:?}");
        assert!(e.contains(&"workspacev2>>11,11".to_string()), "{e:?}");
        assert!(events_between(&new, &new).is_empty());
    }

    #[test]
    fn requests_without_wm() {
        assert_eq!(handle_request("splash"), "Hyprland on Windows");
        assert!(handle_request("j/version").contains("\"branch\": \"aether\""));
        assert!(handle_request("[[BATCH]]splash;splash").contains("\n\n"));
        assert!(handle_request("nonsense").starts_with("unknown request"));
        assert_eq!(handle_request("j/clients"), "[]");
    }
}
