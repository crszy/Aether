//! hyprland.conf parser (Hyprland's own syntax).
//!
//! Handles `$variables`, `source = file` (with `~` and `*` globs), nested `section { }` blocks,
//! `section:key = value`, `# comments` (`##` is a literal `#`), every `bind` flag combination,
//! `submap = name` / `submap = reset`, `windowrule` / `windowrulev2` (old and new syntax), `bezier`,
//! `animation`, `monitor`, `workspace`, `exec`, `exec-once`, `env`. Unknown keywords are kept in
//! `options` so `getoption` can still answer them.

use std::collections::BTreeMap;
use std::path::Path;
use std::path::PathBuf;

#[derive(Debug, Clone, Default, PartialEq)]
pub struct HyprConf {
    /// every `a:b:c = value` (section path joined with `:`), last one wins
    pub options: BTreeMap<String, String>,
    pub binds: Vec<Bind>,
    pub monitors: Vec<String>,
    pub workspaces: Vec<String>,
    pub window_rules: Vec<WindowRule>,
    pub exec_once: Vec<String>,
    pub exec: Vec<String>,
    pub env: Vec<(String, String)>,
    pub beziers: Vec<Bezier>,
    pub animations: Vec<Animation>,
    /// files read, in order (main file first)
    pub files: Vec<PathBuf>,
    /// "file:line: message" for lines that could not be understood
    pub errors: Vec<String>,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct BindFlags {
    /// `l`: works while locked
    pub locked: bool,
    /// `r`: fires on release
    pub release: bool,
    /// `e`: repeats while held
    pub repeat: bool,
    /// `n`: non-consuming (the key still reaches the app)
    pub non_consuming: bool,
    /// `m`: mouse bind (movewindow / resizewindow drag)
    pub mouse: bool,
    /// `t`: transparent (cannot be shadowed by other binds)
    pub transparent: bool,
    /// `i`: ignore modifiers
    pub ignore_mods: bool,
    /// `s`: separate (key combo with several non-mod keys)
    pub separate: bool,
    /// `d`: has a description (the first argument after the key)
    pub description: bool,
    /// `o`: long press
    pub long_press: bool,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Hash)]
pub struct Mods {
    pub shift: bool,
    pub ctrl: bool,
    pub alt: bool,
    /// SUPER / WIN / LOGO / MOD4
    pub sup: bool,
}

impl Mods {
    pub fn parse(text: &str) -> Mods {
        let mut mods = Mods::default();
        // Hyprland accepts "SUPER SHIFT", "SUPER_SHIFT", "SUPER+SHIFT", "$mainMod SHIFT" (after expansion)
        for part in text.split(|c: char| c == ' ' || c == '_' || c == '+' || c == '&').filter(|p| !p.is_empty()) {
            match part.to_ascii_uppercase().as_str() {
                "SHIFT" => mods.shift = true,
                "CTRL" | "CONTROL" => mods.ctrl = true,
                "ALT" | "MOD1" => mods.alt = true,
                "SUPER" | "WIN" | "LOGO" | "MOD4" | "META" => mods.sup = true,
                _ => {}
            }
        }
        mods
    }
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Bind {
    pub flags: BindFlags,
    pub mods: Mods,
    /// as written: "Q", "Return", "code:10", "mouse:272", "mouse_down", "XF86AudioRaiseVolume"
    pub key: String,
    pub dispatcher: String,
    pub arg: String,
    /// the submap this bind belongs to (None = the global map)
    pub submap: Option<String>,
    pub description: Option<String>,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct WindowRule {
    /// "float", "workspace 3 silent", "opacity 0.9 0.8", "size 800 600", ...
    pub rule: String,
    /// ("class", "^(kitty)$"), ("title", "..."), ("floating", "1") ...
    pub matchers: Vec<(String, String)>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Bezier {
    pub name: String,
    pub points: [f32; 4],
}

#[derive(Debug, Clone, PartialEq)]
pub struct Animation {
    /// "windows", "windowsIn", "workspaces", "fade", "border", "layers", "specialWorkspace", ...
    pub name: String,
    pub enabled: bool,
    /// in deciseconds, like Hyprland (speed 1 = 100 ms)
    pub speed: f32,
    pub curve: String,
    /// "slide", "popin 80%", "slidevert", "fade", ...
    pub style: String,
}

impl HyprConf {
    pub fn get(&self, key: &str) -> Option<&str> {
        self.options.get(key).map(String::as_str)
    }

    pub fn get_i64(&self, key: &str) -> Option<i64> {
        self.get(key).and_then(|v| v.trim().parse::<f64>().ok()).map(|v| v as i64)
    }

    pub fn get_f64(&self, key: &str) -> Option<f64> {
        self.get(key).and_then(|v| v.trim().parse::<f64>().ok())
    }

    pub fn get_bool(&self, key: &str) -> Option<bool> {
        self.get(key).map(|v| matches!(v.trim().to_ascii_lowercase().as_str(), "1" | "true" | "yes" | "on"))
    }

    /// Reads `path` (and everything it sources).
    pub fn load(path: &Path) -> HyprConf {
        let mut conf = HyprConf::default();
        let mut state = ParseState::default();
        conf.read_file(path, &mut state, 0);
        conf
    }

    /// Parses config text (no file access except for `source =` relative to `base`).
    pub fn parse_str(text: &str, base: &Path) -> HyprConf {
        let mut conf = HyprConf::default();
        let mut state = ParseState { dir: base.to_path_buf(), ..Default::default() };
        conf.parse_text(text, "<config>", &mut state, 0);
        conf
    }

    fn read_file(&mut self, path: &Path, state: &mut ParseState, depth: usize) {
        if depth > 16 {
            self.errors.push(format!("{}: source nesting too deep", path.display()));
            return;
        }
        // `source = x.lua`: Hyprland's Lua config, run through hyprlua.exe and added here
        if path.extension().is_some_and(|e| e.eq_ignore_ascii_case("lua")) {
            super::lua::merge(path, self);
            return;
        }
        let text = match std::fs::read_to_string(path) {
            Ok(text) => text,
            Err(error) => {
                self.errors.push(format!("{}: {error}", path.display()));
                return;
            }
        };
        self.files.push(path.to_path_buf());
        let saved_dir = std::mem::replace(&mut state.dir, path.parent().map(Path::to_path_buf).unwrap_or_default());
        let name = path.display().to_string();
        self.parse_text(text.trim_start_matches('\u{feff}'), &name, state, depth);
        state.dir = saved_dir;
    }

    fn parse_text(&mut self, text: &str, file: &str, state: &mut ParseState, depth: usize) {
        for (index, raw) in text.lines().enumerate() {
            let line = strip_comment(raw);
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            let at = || format!("{file}:{}", index + 1);

            // closing braces (possibly several, possibly after a value on the same line is not allowed)
            if line == "}" {
                if state.sections.pop().is_none() {
                    self.errors.push(format!("{}: unmatched }}", at()));
                }
                continue;
            }
            // `name {` opens a section (also `device:name {`, which Hyprland 0.3x accepted)
            if let Some(name) = line.strip_suffix('{') {
                let name = name.trim();
                state.sections.push(name.to_string());
                continue;
            }

            let Some((key, value)) = line.split_once('=') else {
                self.errors.push(format!("{}: expected `key = value`: {line}", at()));
                continue;
            };
            let key = key.trim();
            let value = value.trim();

            // variables are defined at top level and expanded everywhere after
            if let Some(var) = key.strip_prefix('$') {
                let expanded = state.expand(value);
                state.vars.insert(var.to_string(), expanded);
                continue;
            }
            let value = state.expand(value);

            let full_key = if state.sections.is_empty() {
                key.to_string()
            } else {
                format!("{}:{key}", state.sections.join(":"))
            };

            self.keyword(&full_key, key, &value, file, index + 1, state, depth);
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn keyword(
        &mut self,
        full_key: &str,
        key: &str,
        value: &str,
        file: &str,
        line: usize,
        state: &mut ParseState,
        depth: usize,
    ) {
        let at = || format!("{file}:{line}");
        match key {
            "source" => {
                for path in expand_source(value, &state.dir) {
                    self.read_file(&path, state, depth + 1);
                }
            }
            "monitor" => self.monitors.push(value.to_string()),
            "workspace" => self.workspaces.push(value.to_string()),
            "exec-once" => self.exec_once.push(value.to_string()),
            "exec" => self.exec.push(value.to_string()),
            "env" => match value.split_once(',') {
                Some((name, val)) => self.env.push((name.trim().to_string(), val.trim().to_string())),
                None => self.errors.push(format!("{}: env needs `NAME,value`", at())),
            },
            "submap" => {
                state.submap = match value {
                    "reset" | "" => None,
                    name => Some(name.split(',').next().unwrap_or(name).trim().to_string()),
                };
            }
            "bezier" => {
                let parts: Vec<&str> = value.split(',').map(str::trim).collect();
                let nums: Vec<f32> = parts.iter().skip(1).filter_map(|p| p.parse().ok()).collect();
                if parts.len() == 5 && nums.len() == 4 {
                    self.beziers.push(Bezier { name: parts[0].to_string(), points: [nums[0], nums[1], nums[2], nums[3]] });
                } else {
                    self.errors.push(format!("{}: bezier needs `name, x1, y1, x2, y2`", at()));
                }
            }
            "animation" => {
                let parts: Vec<&str> = value.split(',').map(str::trim).collect();
                if parts.len() < 2 {
                    self.errors.push(format!("{}: animation needs `name, on/off, speed, curve[, style]`", at()));
                    return;
                }
                let enabled = parts[1] != "0";
                self.animations.push(Animation {
                    name: parts[0].to_string(),
                    enabled,
                    speed: parts.get(2).and_then(|s| s.parse().ok()).unwrap_or(5.0),
                    curve: parts.get(3).map(|s| s.to_string()).unwrap_or_else(|| "default".into()),
                    style: parts.get(4..).map(|s| s.join(",")).unwrap_or_default(),
                });
            }
            "windowrule" | "windowrulev2" => match parse_window_rule(key, value) {
                Some(rule) => self.window_rules.push(rule),
                None => self.errors.push(format!("{}: could not read {key}: {value}", at())),
            },
            _ if key.starts_with("bind") && is_bind_keyword(key) => match parse_bind(key, value, state.submap.clone()) {
                Some(bind) => self.binds.push(bind),
                None => self.errors.push(format!("{}: could not read {key}: {value}", at())),
            },
            _ => {
                self.options.insert(full_key.to_string(), value.to_string());
            }
        }
    }
}

#[derive(Default)]
struct ParseState {
    vars: BTreeMap<String, String>,
    sections: Vec<String>,
    submap: Option<String>,
    dir: PathBuf,
}

impl ParseState {
    /// `$name` -> value; the longest variable name wins (`$mainModShift` before `$mainMod`)
    fn expand(&self, value: &str) -> String {
        if !value.contains('$') {
            return value.to_string();
        }
        let mut names: Vec<&String> = self.vars.keys().collect();
        names.sort_by_key(|n| std::cmp::Reverse(n.len()));
        let mut out = String::with_capacity(value.len());
        let mut rest = value;
        while let Some(pos) = rest.find('$') {
            out.push_str(&rest[..pos]);
            let after = &rest[pos + 1..];
            match names.iter().find(|n| after.starts_with(n.as_str())) {
                Some(name) => {
                    out.push_str(&self.vars[*name]);
                    rest = &after[name.len()..];
                }
                None => {
                    out.push('$');
                    rest = after;
                }
            }
        }
        out.push_str(rest);
        out
    }
}

/// `# comment` ends the line; `##` is a literal `#` (Hyprland's escape)
fn strip_comment(line: &str) -> String {
    let mut out = String::with_capacity(line.len());
    let mut chars = line.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '#' {
            if chars.peek() == Some(&'#') {
                chars.next();
                out.push('#');
                continue;
            }
            break;
        }
        out.push(c);
    }
    out
}

fn home_dir() -> PathBuf {
    std::env::var_os("USERPROFILE").or_else(|| std::env::var_os("HOME")).map(PathBuf::from).unwrap_or_default()
}

fn expand_source(value: &str, dir: &Path) -> Vec<PathBuf> {
    let value = value.trim();
    let path = if let Some(rest) = value.strip_prefix("~/").or_else(|| value.strip_prefix("~\\")) {
        home_dir().join(rest)
    } else if Path::new(value).is_absolute() {
        PathBuf::from(value)
    } else {
        dir.join(value)
    };
    // only a `*` in the file name part is supported (Hyprland uses glob(3))
    let name = path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
    if !name.contains('*') {
        return vec![path];
    }
    let parent = path.parent().map(Path::to_path_buf).unwrap_or_default();
    let (pre, post) = name.split_once('*').unwrap_or((&name, ""));
    let mut found: Vec<PathBuf> = std::fs::read_dir(&parent)
        .into_iter()
        .flatten()
        .flatten()
        .map(|e| e.path())
        .filter(|p| {
            let n = p.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
            p.is_file() && n.starts_with(pre) && n.ends_with(post) && n.len() >= pre.len() + post.len()
        })
        .collect();
    found.sort();
    found
}

fn is_bind_keyword(key: &str) -> bool {
    key[4..].chars().all(|c| "lrenmtisdo".contains(c))
}

fn parse_bind(key: &str, value: &str, submap: Option<String>) -> Option<Bind> {
    let mut flags = BindFlags::default();
    for c in key[4..].chars() {
        match c {
            'l' => flags.locked = true,
            'r' => flags.release = true,
            'e' => flags.repeat = true,
            'n' => flags.non_consuming = true,
            'm' => flags.mouse = true,
            't' => flags.transparent = true,
            'i' => flags.ignore_mods = true,
            's' => flags.separate = true,
            'd' => flags.description = true,
            'o' => flags.long_press = true,
            _ => return None,
        }
    }
    // MODS, key, [description,] dispatcher, args (args may contain commas)
    let mut parts = value.splitn(if flags.description { 5 } else { 4 }, ',').map(str::trim);
    let mods = Mods::parse(parts.next()?);
    let key = parts.next()?.to_string();
    let description = if flags.description { Some(parts.next()?.to_string()) } else { None };
    let dispatcher = parts.next().unwrap_or("").to_string();
    let arg = parts.next().unwrap_or("").to_string();
    if key.is_empty() && dispatcher.is_empty() {
        return None;
    }
    Some(Bind { flags, mods, key, dispatcher, arg, submap, description })
}

/// windowrule = float, ^(kitty)$                         (v1: rule, class regex or `title:regex`)
/// windowrulev2 = float, class:^(kitty)$, title:^(.*)$   (v2)
/// windowrule = float, class:^(kitty)$                   (Hyprland 0.48+: v1 keyword, v2 syntax)
fn parse_window_rule(keyword: &str, value: &str) -> Option<WindowRule> {
    let mut parts = value.split(',').map(str::trim);
    let rule = parts.next()?.to_string();
    if rule.is_empty() {
        return None;
    }
    let mut matchers = Vec::new();
    for part in parts {
        if part.is_empty() {
            continue;
        }
        match part.split_once(':') {
            Some((name, regex)) if !name.contains(' ') && !name.is_empty() => {
                matchers.push((name.trim().to_string(), regex.trim().to_string()))
            }
            _ if keyword == "windowrule" => matchers.push(("class".to_string(), part.to_string())),
            _ => return None,
        }
    }
    Some(WindowRule { rule, matchers })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(text: &str) -> HyprConf {
        HyprConf::parse_str(text, Path::new("."))
    }

    #[test]
    fn variables_sections_and_comments() {
        let c = parse(
            "$mainMod = SUPER # the windows key\n$gap = 5\ngeneral {\n  gaps_in = $gap\n  border_size = 2\n  col.active_border = rgba(33ccffee) rgba(00ff99ee) 45deg\n}\ndecoration:rounding = 10\nmisc { disable_hyprland_logo = true }\n",
        );
        assert_eq!(c.get("general:gaps_in"), Some("5"));
        assert_eq!(c.get("general:border_size"), Some("2"));
        assert_eq!(c.get("general:col.active_border"), Some("rgba(33ccffee) rgba(00ff99ee) 45deg"));
        assert_eq!(c.get_i64("decoration:rounding"), Some(10));
        assert!(c.errors.is_empty() || c.errors.iter().all(|e| e.contains("misc")), "{:?}", c.errors);
    }

    #[test]
    fn nested_sections() {
        let c = parse("decoration {\n  blur {\n    enabled = true\n    size = 8\n  }\n}\n");
        assert_eq!(c.get_bool("decoration:blur:enabled"), Some(true));
        assert_eq!(c.get_i64("decoration:blur:size"), Some(8));
    }

    #[test]
    fn binds_with_flags_and_submaps() {
        let c = parse(
            "$mainMod = SUPER\nbind = $mainMod, Q, exec, kitty\nbind = $mainMod SHIFT, 1, movetoworkspace, 1\nbindel = , XF86AudioRaiseVolume, exec, wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+\nbindm = $mainMod, mouse:272, movewindow\nbindd = SUPER, E, File manager, exec, explorer.exe\nbind = SUPER, R, submap, resize\nsubmap = resize\nbinde = , right, resizeactive, 10 0\nbind = , escape, submap, reset\nsubmap = reset\nbind = SUPER, X, exec, echo a,b,c\n",
        );
        assert_eq!(c.binds.len(), 9);
        let q = &c.binds[0];
        assert!(q.mods.sup && !q.mods.shift);
        assert_eq!((q.key.as_str(), q.dispatcher.as_str(), q.arg.as_str()), ("Q", "exec", "kitty"));
        assert!(c.binds[1].mods.sup && c.binds[1].mods.shift);
        assert!(c.binds[2].flags.repeat && c.binds[2].flags.locked);
        assert!(c.binds[3].flags.mouse);
        assert_eq!(c.binds[4].description.as_deref(), Some("File manager"));
        assert_eq!(c.binds[4].dispatcher, "exec");
        assert_eq!(c.binds[6].submap.as_deref(), Some("resize"));
        assert!(c.binds[6].flags.repeat);
        assert_eq!(c.binds[7].submap.as_deref(), Some("resize")); // escape lives in the submap too
        assert_eq!(c.binds[8].submap, None);
        assert_eq!(c.binds[8].arg, "echo a,b,c"); // commas inside the arguments survive
    }

    #[test]
    fn window_rules_all_syntaxes() {
        let c = parse(
            "windowrule = float, ^(pavucontrol)$\nwindowrule = float, title:^(Picture-in-Picture)$\nwindowrulev2 = opacity 0.9 0.8, class:^(kitty)$, title:^(.*)$\nwindowrule = workspace 3 silent, class:^(discord)$\n",
        );
        assert_eq!(c.window_rules.len(), 4);
        assert_eq!(c.window_rules[0].matchers, vec![("class".into(), "^(pavucontrol)$".into())]);
        assert_eq!(c.window_rules[1].matchers, vec![("title".into(), "^(Picture-in-Picture)$".into())]);
        assert_eq!(c.window_rules[2].rule, "opacity 0.9 0.8");
        assert_eq!(c.window_rules[2].matchers.len(), 2);
        assert_eq!(c.window_rules[3].rule, "workspace 3 silent");
    }

    #[test]
    fn beziers_animations_and_misc() {
        let c = parse(
            "animations {\n  enabled = yes\n  bezier = myBezier, 0.05, 0.9, 0.1, 1.05\n  animation = windows, 1, 7, myBezier\n  animation = windowsOut, 1, 7, default, popin 80%\n  animation = workspaces, 1, 6, default, slidevert\n}\nmonitor = ,preferred,auto,1\nworkspace = 1, monitor:DP-1, default:true\nexec-once = waybar\nexec = notify-send hi\nenv = XCURSOR_SIZE,24\n## not a comment\n",
        );
        assert_eq!(c.beziers, vec![Bezier { name: "myBezier".into(), points: [0.05, 0.9, 0.1, 1.05] }]);
        assert_eq!(c.animations.len(), 3);
        assert_eq!(c.animations[1].style, "popin 80%");
        assert_eq!(c.animations[2].speed, 6.0);
        assert_eq!(c.get_bool("animations:enabled"), Some(true));
        assert_eq!(c.monitors.len(), 1);
        assert_eq!(c.workspaces, vec!["1, monitor:DP-1, default:true".to_string()]);
        assert_eq!(c.exec_once, vec!["waybar".to_string()]);
        assert_eq!(c.env, vec![("XCURSOR_SIZE".into(), "24".into())]);
    }

    #[test]
    fn hash_escape() {
        let c = parse("bind = SUPER, C, exec, echo ##hash\n");
        assert_eq!(c.binds[0].arg, "echo #hash");
    }

    #[test]
    fn source_relative_and_glob() {
        let dir = std::env::temp_dir().join(format!("hyprconf-test-{}", std::process::id()));
        std::fs::create_dir_all(dir.join("conf.d")).unwrap();
        std::fs::write(dir.join("hyprland.conf"), "$m = SUPER\nsource = conf.d/*.conf\ngeneral:gaps_out = 20\n").unwrap();
        std::fs::write(dir.join("conf.d/a.conf"), "bind = $m, A, exec, a\n").unwrap();
        std::fs::write(dir.join("conf.d/b.conf"), "bind = $m, B, exec, b\n").unwrap();
        let c = HyprConf::load(&dir.join("hyprland.conf"));
        assert_eq!(c.binds.iter().map(|b| b.key.as_str()).collect::<Vec<_>>(), vec!["A", "B"]);
        assert!(c.binds[0].mods.sup);
        assert_eq!(c.get_i64("general:gaps_out"), Some(20));
        assert_eq!(c.files.len(), 3);
        let _ = std::fs::remove_dir_all(&dir);
    }
}

#[cfg(test)]
mod real_config {
    /// `cargo test -p komorebi --lib hypr::conf::real_config -- --ignored --nocapture`: parses your own
    /// hyprland.conf and prints what was understood
    #[test]
    #[ignore]
    fn print_user_config() {
        let path = crate::hypr::config_path();
        let c = super::HyprConf::load(&path);
        println!("files: {:?}", c.files);
        println!("options: {}", c.options.len());
        for (k, v) in &c.options {
            println!("  {k} = {v}");
        }
        println!("binds: {}", c.binds.len());
        for b in &c.binds {
            println!("  {:?} {} -> {} {} (submap {:?})", b.mods, b.key, b.dispatcher, b.arg, b.submap);
        }
        println!("rules: {:?}\nexec-once: {:?}\nanimations: {:?}\nbeziers: {:?}", c.window_rules, c.exec_once, c.animations, c.beziers);
        println!("errors: {:?}", c.errors);
    }
}
