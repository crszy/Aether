#!/usr/bin/env bash
# Zenith theme generator & preset engine.
#
# Supports:
#   zenith-theme.sh --preset <name> [--accent <hex>]   apply a curated dotfile theme
#   zenith-theme.sh --accent <hex>                     override active theme accent
#   zenith-theme.sh --dynamic [wall_path]              derive palette from wallpaper (matugen)
#   zenith-theme.sh --list                             list available presets as JSON
#   zenith-theme.sh --current                          show active theme status as JSON
#   zenith-theme.sh [wall_path]                        backwards-compatible wallpaper mode

set -uo pipefail

SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/zenith"
STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/zenith"
PRESETS_DIR="$SHELL_DIR/themes/presets"
PALETTE="$CACHE_DIR/colors.json"
TEMPLATE="$SHELL_DIR/themes/matugen/colors.json"
HYPR_TEMPLATE="$SHELL_DIR/themes/matugen/hyprland-scheme.lua"
HYPR_SCHEME="$HOME/.config/hypr/scheme/current.lua"
WALL_CACHE="$HOME/.config/current_wallpaper.txt"
THEME_CONFIG="$STATE_DIR/theme_config.json"

log()      { printf '  -> %s\n' "$*"; }
log_step() { printf '\033[1;34m[STEP]\033[0m %s\n' "$*"; }
log_ok()   { printf '\033[1;32m[OK]\033[0m %s\n' "$*"; }
log_err()  { printf '\033[1;31m[ERR]\033[0m %s\n' "$*" >&2; }

mkdir -p "$CACHE_DIR" "$STATE_DIR"

validate_scheme() {
    local file="$1"
    [ -s "$file" ] || { echo "empty or missing" >&2; return 1; }

    python3 - "$file" <<'SCHEME_CHECK' || return 1
import re, sys
s = open(sys.argv[1]).read()
rows = [l for l in s.splitlines() if re.match(r'\s+\w+\s*=', l)]
problems = []
if not rows:                                       problems.append("no entries")
if any(not l.rstrip().endswith(',') for l in rows): problems.append("missing trailing comma")
if "{{" in s:                                      problems.append("unrendered template placeholder")
if s.count("{") != s.count("}"):                   problems.append("unbalanced braces")
if problems:
    sys.stderr.write("; ".join(problems) + "\n")
    sys.exit(1)
SCHEME_CHECK

    local lua_bin
    for lua_bin in lua luajit lua5.4 lua5.3; do
        if command -v "$lua_bin" >/dev/null 2>&1; then
            "$lua_bin" -e "assert(type(dofile('$file'))=='table')" 2>/dev/null || {
                echo "not valid Lua" >&2
                return 1
            }
            break
        fi
    done
    return 0
}

if [[ "${1:-}" == "--validate" ]]; then
    validate_scheme "${2:-}" && { echo "ok"; exit 0; } || exit 1
fi

if [[ "${1:-}" == "--list" ]]; then
    python3 - "$PRESETS_DIR" <<'LIST_PRESETS'
import glob, json, os, sys
pdir = sys.argv[1]
items = []
for p in sorted(glob.glob(os.path.join(pdir, "*.json"))):
    try:
        d = json.load(open(p))
        items.append({
            "id": d.get("id", os.path.splitext(os.path.basename(p))[0]),
            "name": d.get("name", os.path.basename(p)),
            "category": d.get("category", "Custom"),
            "background": d.get("background", "#1e1e2e"),
            "surface": d.get("surface", "#181825"),
            "primary": d.get("primary", "#cba6f7"),
            "secondary": d.get("secondary", "#89b4fa")
        })
    except Exception:
        pass
print(json.dumps(items))
LIST_PRESETS
    exit 0
fi

if [[ "${1:-}" == "--current" ]]; then
    if [[ -f "$THEME_CONFIG" ]]; then
        cat "$THEME_CONFIG"
    else
        echo '{"mode":"dynamic","preset":"","accent":""}'
    fi
    exit 0
fi

# Parsing Arguments
MODE="dynamic"
PRESET_NAME=""
ACCENT_OVERRIDE=""
WALLPAPER=""
SOURCE_COLOR=""
PREFER="saturation"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset|-p)
            MODE="preset"
            PRESET_NAME="${2:-}"
            shift 2
            ;;
        --accent|-a)
            ACCENT_OVERRIDE="${2:-}"
            shift 2
            ;;
        --dynamic|-d)
            MODE="dynamic"
            if [[ -n "${2:-}" && ! "$2" =~ ^-- ]]; then
                WALLPAPER="$2"
                shift 2
            else
                shift 1
            fi
            ;;
        --autoselect)
            shift 1
            ;;
        --color)
            SOURCE_COLOR="${2:-}"
            shift 2
            ;;
        *)
            if [[ -z "$WALLPAPER" && ! "$1" =~ ^-- ]]; then
                WALLPAPER="$1"
            fi
            shift 1
            ;;
    esac
done

PALETTE_STAGE="$CACHE_DIR/colors.json.new"
HYPR_STAGE="$CACHE_DIR/hypr-scheme.lua.new"
rm -f "$PALETTE_STAGE" "$HYPR_STAGE"
trap 'rm -f "$PALETTE_STAGE" "$HYPR_STAGE"' EXIT

if [[ "$MODE" == "preset" ]]; then
    PRESET_FILE="$PRESETS_DIR/${PRESET_NAME}.json"
    if [[ ! -f "$PRESET_FILE" ]]; then
        # Try matching by id without extension or search
        MATCH=$(find "$PRESETS_DIR" -name "${PRESET_NAME}*.json" | head -n1)
        if [[ -n "$MATCH" && -f "$MATCH" ]]; then
            PRESET_FILE="$MATCH"
        else
            log_err "Preset not found: $PRESET_NAME"
            exit 1
        fi
    fi

    log_step "Applying theme preset: $(basename "$PRESET_FILE" .json)"

    # Render palette stage and lua stage via Python
    python3 - "$PRESET_FILE" "$PALETTE_STAGE" "$HYPR_STAGE" "$ACCENT_OVERRIDE" <<'GEN_PRESET'
import json, sys

src_file = sys.argv[1]
pal_out = sys.argv[2]
hypr_out = sys.argv[3]
accent = sys.argv[4].strip() if len(sys.argv) > 4 else ""

with open(src_file) as f:
    data = json.load(f)

if accent and accent.startswith("#"):
    data["primary"] = accent
    data["accent"] = accent

# Save colors.json
with open(pal_out, "w") as f:
    json.dump(data, f, indent=2)

# Generate Hyprland scheme lua
def strip(c):
    return c.lstrip("#") if isinstance(c, str) else ""

bg = strip(data.get("background", "1e1e2e"))
fg = strip(data.get("on_background", "cdd6f4"))
surf = strip(data.get("surface", "1e1e2e"))
pri = strip(data.get("primary", "cba6f7"))
sec = strip(data.get("secondary", "89b4fa"))
ter = strip(data.get("tertiary", "a6e3a1"))
err = strip(data.get("error", "f38ba8"))
outl = strip(data.get("outline", "585b70"))

lua = f"""return {{
    background                      = "{bg}",
    onBackground                    = "{fg}",
    surface                         = "{surf}",
    surfaceDim                      = "{strip(data.get("surface_container_low", bg))}",
    surfaceBright                   = "{strip(data.get("surface_container_highest", bg))}",
    surfaceContainerLowest          = "{strip(data.get("surface_container_lowest", bg))}",
    surfaceContainerLow             = "{strip(data.get("surface_container_low", bg))}",
    surfaceContainer                = "{strip(data.get("surface_container", bg))}",
    surfaceContainerHigh            = "{strip(data.get("surface_container_high", bg))}",
    surfaceContainerHighest         = "{strip(data.get("surface_container_highest", bg))}",
    onSurface                       = "{fg}",
    surfaceVariant                  = "{strip(data.get("surface_variant", surf))}",
    onSurfaceVariant                = "{strip(data.get("on_surface_variant", fg))}",
    outline                         = "{outl}",
    outlineVariant                  = "{strip(data.get("outline_variant", outl))}",
    primary                         = "{pri}",
    onPrimary                       = "{strip(data.get("on_primary", "11111b"))}",
    primaryContainer                = "{strip(data.get("primary_container", pri))}",
    onPrimaryContainer              = "{strip(data.get("on_primary_container", fg))}",
    secondary                       = "{sec}",
    onSecondary                     = "{strip(data.get("on_secondary", "11111b"))}",
    secondaryContainer              = "{strip(data.get("secondary_container", sec))}",
    onSecondaryContainer            = "{strip(data.get("on_secondary_container", fg))}",
    tertiary                        = "{ter}",
    onTertiary                      = "{strip(data.get("on_tertiary", "11111b"))}",
    tertiaryContainer               = "{strip(data.get("tertiary_container", ter))}",
    onTertiaryContainer             = "{strip(data.get("on_tertiary_container", fg))}",
    error                           = "{err}",
    onError                         = "{strip(data.get("on_error", "11111b"))}",
    accent                          = "{strip(data.get("accent", pri))}",
    base                            = "{bg}",
    mantle                          = "{strip(data.get("surface_container_low", bg))}",
    crust                           = "{strip(data.get("surface_container_lowest", bg))}",
}}
"""
with open(hypr_out, "w") as f:
    f.write(lua)
GEN_PRESET

    THEME_HYPRLAND=1

else
    # --- DYNAMIC WALLPAPER MODE (Matugen) ---
    if [[ -z "$WALLPAPER" ]]; then
        if [[ -f "$WALL_CACHE" ]]; then
            WALLPAPER="$(cat "$WALL_CACHE")"
            log "No path given, using current wallpaper: $WALLPAPER"
        else
            log_err "No wallpaper given and $WALL_CACHE does not exist."
            exit 1
        fi
    fi

    [[ -f "$WALLPAPER" ]] || { log_err "Wallpaper does not exist: $WALLPAPER"; exit 1; }
    command -v matugen >/dev/null 2>&1 || { log_err "matugen is not installed."; exit 1; }
    [[ -f "$TEMPLATE" ]] || { log_err "Palette template missing: $TEMPLATE"; exit 1; }

    CONFIG="$(mktemp -t zenith-matugen-XXXXXX.toml)"
    trap 'rm -f "$CONFIG" "$PALETTE_STAGE" "$HYPR_STAGE"' EXIT
    cat > "$CONFIG" <<EOF
[config]

[templates.zenith_shell]
input_path = "$TEMPLATE"
output_path = "$PALETTE_STAGE"
EOF

    if [[ -f "$HYPR_TEMPLATE" && -d "$(dirname "$HYPR_SCHEME")" ]]; then
        cat >> "$CONFIG" <<EOF

[templates.hyprland_scheme]
input_path = "$HYPR_TEMPLATE"
output_path = "$HYPR_STAGE"
EOF
        THEME_HYPRLAND=1
    fi

    log_step "Generating palette from $(basename "$WALLPAPER")"

    matugen_args=(image "$WALLPAPER" --config "$CONFIG" --mode dark --prefer "$PREFER")
    [[ -n "$SOURCE_COLOR" ]] && matugen_args+=(--color "$SOURCE_COLOR")

    if ! matugen "${matugen_args[@]}"; then
        log_err "matugen failed; the previous theme is still in place."
        exit 1
    fi
fi

# --- Validate before installing ---
if ! python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$PALETTE_STAGE" 2>/dev/null; then
    log_err "Generated palette is not valid JSON; keeping the previous theme."
    exit 1
fi
mv -f "$PALETTE_STAGE" "$PALETTE"
log_ok "Palette written to $PALETTE"

if [[ "${THEME_HYPRLAND:-0}" == "1" && -f "$HYPR_STAGE" ]]; then
    if ! validate_scheme "$HYPR_STAGE"; then
        log_err "Generated Hyprland scheme is malformed; keeping the existing one."
        exit 1
    fi

    [[ -f "$HYPR_SCHEME" ]] && cp -f "$HYPR_SCHEME" "$CACHE_DIR/hypr-scheme.previous.lua"
    mv -f "$HYPR_STAGE" "$HYPR_SCHEME"
    log "Hyprland scheme written to $HYPR_SCHEME"
fi

# Write runtime config state
cat > "$THEME_CONFIG" <<EOF
{
  "mode": "$MODE",
  "preset": "$PRESET_NAME",
  "accent": "$ACCENT_OVERRIDE",
  "wallpaper": "${WALLPAPER:-}"
}
EOF

# --- Refresh live components ---
log_step "Refreshing compositor and apps"

if command -v hyprctl >/dev/null 2>&1 && [[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]]; then
    hyprctl reload >/dev/null 2>&1 && log "Hyprland reloaded"
fi

killall -USR1 kitty 2>/dev/null && log "kitty reloaded"

if command -v gsettings >/dev/null 2>&1; then
    gsettings set org.gnome.desktop.interface color-scheme 'prefer-light' 2>/dev/null
    sleep 0.1
    gsettings set org.gnome.desktop.interface color-scheme 'prefer-dark' 2>/dev/null
fi

log_ok "Zenith theme updated successfully."
