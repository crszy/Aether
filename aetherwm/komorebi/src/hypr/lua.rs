//! hyprland.lua: Hyprland's Lua config. Aether's hyprlua.exe (Lua 5.5 with a stand-in `hl` table, shipped next to
//! qs.exe) runs the config and prints what it set as JSON; this turns that into the same HyprConf the
//! hyprland.conf parser makes - options, binds (hl.dsp.* calls -> classic dispatchers), exec-once, window rules,
//! workspace rules.

use std::path::Path;
use std::path::PathBuf;

use serde_json::Value;

use super::conf::Bind;
use super::conf::BindFlags;
use super::conf::HyprConf;
use super::conf::Mods;
use super::conf::WindowRule;

/// hyprlua.exe: next to qs.exe in Aether's package (CAELESTIA_PACKAGE_DIR, set by Aether's launcher)
pub fn hyprlua_exe() -> Option<PathBuf> {
    let package = std::env::var_os("CAELESTIA_PACKAGE_DIR").map(PathBuf::from)?;
    let exe = package.join("qs").join("hyprlua.exe");
    exe.is_file().then_some(exe)
}

pub fn load(path: &Path) -> HyprConf {
    let mut conf = HyprConf::default();
    merge(path, &mut conf);
    conf
}

/// runs a Lua config file and adds what it sets to `conf` (`source = x.lua`, Ryoku's Hub settings.lua)
pub fn merge(path: &Path, conf: &mut HyprConf) {
    let Some(exe) = hyprlua_exe() else {
        conf.errors.push(format!("{}: hyprlua.exe not found (it comes with Aether)", path.display()));
        return;
    };
    use std::os::windows::process::CommandExt;
    let output = std::process::Command::new(exe).arg(path).creation_flags(0x0800_0000).output();
    let output = match output {
        Ok(o) => o,
        Err(error) => {
            conf.errors.push(format!("{}: {error}", path.display()));
            return;
        }
    };
    if !output.status.success() {
        conf.errors.push(format!("{}: {}", path.display(), String::from_utf8_lossy(&output.stderr).trim()));
        return;
    }
    match serde_json::from_slice::<Value>(&output.stdout) {
        Ok(json) => from_json(&json, conf),
        Err(error) => conf.errors.push(format!("{}: hyprlua output: {error}", path.display())),
    }
    conf.files.push(path.to_path_buf());
}

fn text(v: &Value) -> String {
    match v {
        Value::String(s) => s.clone(),
        Value::Null => String::new(),
        other => other.to_string(),
    }
}

pub fn from_json(json: &Value, conf: &mut HyprConf) {
    if let Some(options) = json["options"].as_object() {
        for (k, v) in options {
            conf.options.insert(k.clone(), text(v));
        }
    }
    for cmd in json["execOnce"].as_array().into_iter().flatten() {
        let cmd = text(cmd);
        if !cmd.is_empty() {
            conf.exec_once.push(cmd);
        }
    }
    for bind in json["binds"].as_array().into_iter().flatten() {
        let keys = text(&bind["keys"]);
        let parts: Vec<&str> = keys.split('+').map(str::trim).filter(|p| !p.is_empty()).collect();
        let Some((key, mods)) = parts.split_last() else { continue };
        let opts: Vec<String> = bind["opts"].as_array().into_iter().flatten().map(text).collect();
        let has = |o: &str| opts.iter().any(|x| x.eq_ignore_ascii_case(o));
        let mut flags = BindFlags {
            repeat: has("repeating") || has("repeat"),
            locked: has("locked"),
            release: has("release"),
            non_consuming: has("non_consuming") || has("nonconsuming"),
            ..Default::default()
        };
        let (dispatcher, arg) = translate_call(&text(&bind["call"]));
        if key.starts_with("mouse:") {
            flags.mouse = matches!(dispatcher.as_str(), "movewindow" | "resizewindow");
        }
        conf.binds.push(Bind {
            flags,
            mods: Mods::parse(&mods.join(" ")),
            key: key.to_string(),
            dispatcher,
            arg,
            submap: bind.get("submap").map(text).filter(|s| !s.is_empty()),
            description: bind.get("description").map(text).filter(|s| !s.is_empty()),
        });
    }
    for rule in json["rules"].as_array().into_iter().flatten() {
        let Some(object) = rule.as_object() else { continue };
        let matchers: Vec<(String, String)> = object
            .get("match")
            .and_then(Value::as_object)
            .into_iter()
            .flatten()
            .filter_map(|(k, v)| v.as_str().map(|s| (k.clone(), s.to_string())))
            .collect();
        for (key, value) in object {
            if matches!(key.as_str(), "match" | "name") {
                continue;
            }
            let effect = match value {
                Value::Bool(true) => key.clone(),
                Value::Bool(false) => continue,
                other => format!("{key} {}", text(other)),
            };
            conf.window_rules.push(WindowRule { rule: effect, matchers: matchers.clone() });
        }
    }
    for rule in json["workspaceRules"].as_array().into_iter().flatten() {
        let Some(object) = rule.as_object() else { continue };
        let Some(id) = object.get("workspace").or_else(|| object.get("id")).map(text) else { continue };
        let mut line = id;
        for (key, value) in object {
            let hypr_key = match key.as_str() {
                "gaps_in" | "gapsin" => "gapsin",
                "gaps_out" | "gapsout" => "gapsout",
                "default_name" | "defaultName" => "defaultName",
                "on_created_empty" | "on-created-empty" => "on-created-empty",
                _ => continue,
            };
            line.push_str(&format!(", {hypr_key}:{}", text(value)));
        }
        conf.workspaces.push(line);
    }
}

/// the `{ key = value, ... }` table of a call as pairs ("left" for direction = "left")
fn table(args: &str) -> Vec<(String, String)> {
    let inner = args.trim().trim_start_matches('{').trim_end_matches('}');
    inner
        .split(',')
        .filter_map(|pair| {
            let (k, v) = pair.split_once('=')?;
            Some((k.trim().to_string(), v.trim().trim_matches('"').to_string()))
        })
        .collect()
}

fn get<'a>(pairs: &'a [(String, String)], key: &str) -> Option<&'a str> {
    pairs.iter().find(|(k, _)| k == key).map(|(_, v)| v.as_str())
}

fn dir(v: &str) -> String {
    match v {
        "left" => "l",
        "right" => "r",
        "up" => "u",
        "down" => "d",
        other => other,
    }
    .to_string()
}

/// `hl.dsp.focus({ workspace = 1 })` -> ("workspace", "1"); unknown ones keep their text (logged as unsupported)
pub fn translate_call(call: &str) -> (String, String) {
    let call = call.trim();
    let (name, args) = match call.split_once('(') {
        Some((n, a)) => (n.trim(), a.trim_end().trim_end_matches(')').trim()),
        None => (call, ""),
    };
    let string_arg = || args.trim().trim_matches('"').to_string();
    let t = table(args);
    let pair = |d: &str, a: String| (d.to_string(), a);
    match name {
        "hl.dsp.exec_cmd" | "hl.dsp.exec" => pair("exec", string_arg()),
        "hl.dsp.exit" => pair("exit", String::new()),
        "hl.dsp.window.close" | "hl.dsp.window.kill" => pair("killactive", String::new()),
        "hl.dsp.window.float" => pair("togglefloating", String::new()),
        "hl.dsp.window.pseudo" => pair("pseudo", String::new()),
        "hl.dsp.window.pin" => pair("pin", String::new()),
        "hl.dsp.window.center" => pair("centerwindow", String::new()),
        "hl.dsp.window.fullscreen" => {
            let mode = get(&t, "mode").unwrap_or("fullscreen");
            pair("fullscreen", if matches!(mode, "maximized" | "maximize" | "1") { "1" } else { "0" }.to_string())
        }
        "hl.dsp.layout" => pair("layoutmsg", string_arg()),
        "hl.dsp.focus" => {
            if let Some(w) = get(&t, "workspace") {
                pair("workspace", w.to_string())
            } else if let Some(d) = get(&t, "direction") {
                pair("movefocus", dir(d))
            } else if let Some(m) = get(&t, "monitor") {
                pair("focusmonitor", m.to_string())
            } else {
                pair("cyclenext", String::new())
            }
        }
        "hl.dsp.window.move" => {
            if let Some(w) = get(&t, "workspace") {
                let silent = get(&t, "follow") == Some("false");
                pair(if silent { "movetoworkspacesilent" } else { "movetoworkspace" }, w.to_string())
            } else if let Some(d) = get(&t, "direction") {
                pair("movewindow", dir(d))
            } else if let Some(m) = get(&t, "monitor") {
                pair("movewindow", format!("mon:{m}"))
            } else {
                pair("movewindow", String::new())
            }
        }
        "hl.dsp.window.swap" => pair("swapwindow", dir(get(&t, "direction").unwrap_or("l"))),
        "hl.dsp.window.drag" => pair("movewindow", String::new()),
        "hl.dsp.window.resize" => match (get(&t, "x"), get(&t, "y")) {
            (Some(x), Some(y)) => pair("resizeactive", format!("{x} {y}")),
            _ => pair("resizewindow", String::new()),
        },
        "hl.dsp.workspace.toggle_special" | "hl.dsp.workspace.special" => pair("togglespecialworkspace", string_arg()),
        "hl.dsp.submap" => pair("submap", string_arg()),
        other => (other.to_string(), args.to_string()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn calls() {
        assert_eq!(translate_call("hl.dsp.exec_cmd(\"kitty\")"), ("exec".into(), "kitty".into()));
        assert_eq!(translate_call("hl.dsp.window.close()"), ("killactive".into(), "".into()));
        assert_eq!(translate_call("hl.dsp.focus({ workspace = 1 })"), ("workspace".into(), "1".into()));
        assert_eq!(translate_call("hl.dsp.focus({ direction = \"left\" })"), ("movefocus".into(), "l".into()));
        assert_eq!(translate_call("hl.dsp.window.move({ workspace = 3, follow = false })"), ("movetoworkspacesilent".into(), "3".into()));
        assert_eq!(translate_call("hl.dsp.layout(\"togglesplit\")"), ("layoutmsg".into(), "togglesplit".into()));
        assert_eq!(translate_call("hl.dsp.workspace.toggle_special(\"magic\")"), ("togglespecialworkspace".into(), "magic".into()));
        assert_eq!(translate_call("hl.dsp.window.resize({ x = 40, y = 0 })"), ("resizeactive".into(), "40 0".into()));
    }

    #[test]
    fn json_to_conf() {
        let json: Value = serde_json::from_str(
            r#"{"options":{"general:gaps_in":"5","general:layout":"dwindle"},
                "binds":[{"call":"hl.dsp.exec_cmd(\"kitty\")","keys":"SUPER + Q","opts":[]},
                         {"call":"hl.dsp.window.drag()","keys":"SUPER + mouse:272","opts":[]},
                         {"call":"hl.dsp.window.resize({ x = 40, y = 0 })","keys":"SUPER + CTRL + right","opts":["repeating"]}],
                "execOnce":["waybar"],
                "rules":[{"float":true,"match":{"class":"hyprland-run"},"move":"20 40","name":"x"}],
                "workspaceRules":[{"workspace":"3","gaps_in":2}]}"#,
        )
        .unwrap();
        let mut conf = HyprConf::default();
        from_json(&json, &mut conf);
        assert_eq!(conf.get_i64("general:gaps_in"), Some(5));
        assert_eq!(conf.binds.len(), 3);
        assert!(conf.binds[0].mods.sup && conf.binds[0].key == "Q" && conf.binds[0].dispatcher == "exec");
        assert!(conf.binds[1].flags.mouse && conf.binds[1].dispatcher == "movewindow");
        assert!(conf.binds[2].flags.repeat && conf.binds[2].mods.ctrl && conf.binds[2].arg == "40 0");
        assert_eq!(conf.exec_once, vec!["waybar".to_string()]);
        assert_eq!(conf.window_rules.len(), 2);
        assert!(conf.window_rules.iter().any(|r| r.rule == "float"));
        assert!(conf.window_rules.iter().any(|r| r.rule == "move 20 40"));
        assert_eq!(conf.workspaces, vec!["3, gapsin:2".to_string()]);
    }
}

#[cfg(test)]
mod example {
    /// the real hyprlua.exe output of Hyprland's example hyprland.lua (dotfiles/hyprlua/out/example.json)
    #[test]
    fn hyprland_example_config() {
        let path = "../../dotfiles/hyprlua/out/example.json";
        let Ok(text) = std::fs::read_to_string(path) else { return };
        let json: serde_json::Value = serde_json::from_str(&text).unwrap();
        let mut conf = super::HyprConf::default();
        super::from_json(&json, &mut conf);
        let unknown: Vec<String> = conf
            .binds
            .iter()
            .filter(|b| matches!(crate::hypr::dispatch::translate(&b.dispatcher, &b.arg), crate::hypr::dispatch::Action::Unsupported(_)))
            .map(|b| format!("{} {}", b.dispatcher, b.arg))
            .collect();
        println!("binds {}, unsupported: {unknown:?}, rules {}, options {}", conf.binds.len(), conf.window_rules.len(), conf.options.len());
        assert!(conf.binds.len() > 20);
        assert!(conf.binds.iter().any(|b| b.flags.mouse));
        // only Hyprland features that are not on Windows / not done yet may stay unsupported
        assert!(unknown.iter().all(|u| u.starts_with("pseudo") || u.starts_with("pin")), "{unknown:?}");
    }
}
