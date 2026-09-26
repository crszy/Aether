# Aether custom widgets

Every `.toml` and `.py` file in this folder shows up in the dashboard's **Add a widget** palette
(dashboard → edit → **+**, section *Custom*). Place it, drag it, resize it, and give it a style with
the palette button (background, image/GIF, shape, radius, opacity, padding). Saving a file reloads it live.

Caelestia's widgets are Quickshell (QML). Quickshell has no Windows build, so Aether has its own
equivalent: a widget is a list of **items** whose properties can be bound to live data.

---

## TOML widgets

```toml
[widget]
name = "My widget"
size = [0.25, 0.4]        # default size, fractions of the dashboard area
refresh_ms = 1000
background = "card"       # card | none | "#rrggbbaa"
image = ""                # a picture or animated GIF behind everything
shape = ""                # draw the background as a Material shape instead of a card
color = ""                # background colour for card/shape
radius = 28               # card corner radius
padding = 0

[[source]]                # optional: run a command on a timer, use its output as {src.NAME}
name = "ip"
command = "curl -s ifconfig.me"
interval_ms = 60000

[[item]]
type = "text"
text = "{time:%H:%M}"
x = "50%"                 # px from the left, or "50%" of the width, or an expression: "w/2 - 10"
y = 20
size = 32
font = "bold"             # regular | medium | bold | big | light | mono | clock | small
align = "center"          # left | center | right
color = "primary"
```

### Item types and their properties

| type | properties |
|---|---|
| `text` | text, x, y, size, font, align, valign (top/middle/bottom), max_width, wrap, max_lines, line_height, glow |
| `rect` | x, y, w, h, radius, color, border_color, border |
| `circle` | x, y (centre), radius, color, thickness (0 = filled) |
| `shape` | x, y (centre), size (radius), shape, morph_to, morph (0..1), spin (degrees), color, image (picture cut to the shape) |
| `ring` / `arc` | x, y, radius, thickness, value (0..1), start, sweep (degrees), color, track, round_cap |
| `bar` | x, y, w, h, value (0..1), vertical, radius, color, track |
| `image` / `gif` | path, x, y, w, h, radius, fit (cover/contain) |
| `icon` | name (any Material Symbols name), x, y (centre), size, color |
| `line` | x, y, x2, y2, thickness, color |
| `graph` | source (cpu/gpu/mem/disk/net), x, y, w, h, color, fill |
| `button` | x, y, w, h, text, icon, color, hover_color, text_color, radius |

Every item also takes: `visible` (expression, 0 hides it), `alpha` (0..1), `on_click`, `id`.

### on_click actions
- `shell:lock`, `launcher`, `dashboard=Media`, `wallpaper=live`, `settings=Effects` — any Aether command
- `run:notepad.exe` — start a program
- `open:https://…` or `open:C:\path` — open a file, folder or link
- `media:play`, `media:next`, `media:prev`
- `tab:Media` — switch dashboard tab

### Bindings (inside `{ }`)
Numbers (usable in expressions): `cpu gpu mem disk` (0..1), `cpu_temp gpu_temp`, `mem_used mem_total disk_used disk_total` (GiB),
`net_down net_up` (KB/s), `battery charging online`, `t` (seconds), `w h` (widget size), `hover`,
`hour minute second day month year weekday`, `media.playing media.pos media.dur media.progress`,
`weather.temp weather.feels weather.humidity weather.wind`, `src.NAME`.

Text: `{time:%H:%M}` `{date:%A, %d %B}` (strftime), `{media.title}` `{media.artist}` `{media.album}` `{media.source}`,
`{media.pos_str}` `{media.dur_str}`, `{lyric}` (the line being sung), `{weather.text}` `{weather.city}` `{weather.icon}`,
`{cpu_name}` `{gpu_name}` `{user}` `{host}` `{uptime}` `{src.NAME}`.

Formatting: `{cpu%}` → `42%`, `{mem_used:%.1f}` → `7.3`. Write `{{` for a literal brace.

Expressions: `+ - * / %`, `( )`, comparisons `> < >= <= == !=`, `cond ? a : b`, and
`sin cos abs min max clamp floor round sqrt mix pulse deg rad`. Example: `spin = "t*30"`,
`color = "{cpu} > 0.8 ? ..."` is not supported for colours — use two items with `visible`.

### Colours
Theme roles: `primary secondary tertiary ink ink2 card card2 surface on_primary error album` (the cover's colour),
or `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgba(r,g,b,a)`.

### Shapes (Material 3 Expressive, as in Caelestia)
circle square slanted oval pill triangle arrow diamond pentagon gem very_sunny sunny cookie4 cookie6 cookie7
cookie9 cookie12 clover4 soft_burst ghostish arch fan semicircle clamshell clover8 burst boom soft_boom
flower puffy puffy_diamond pixel_circle pixel_triangle bun heart

---

## Python widgets

Needs Python 3 on PATH (or set `widgets.python` in config). Start from `example-orb.py`:

```python
# aether: name="My Python widget" size=[0.25, 0.4]
import aether

w = aether.Widget(refresh=1.0, background="card")

@w.draw
def draw(ui, data):
    ui.ring(ui.w/2, ui.h/2, 40, data.stats.cpu, color="primary")
    ui.text(data.media.title or "Nothing playing", 12, ui.h - 30, max_width=ui.w - 24)

@w.click
def click(ev, data):          # ev.x, ev.y, ev.button, ev.id (the id of the item clicked)
    aether.shell("dashboard=Media")

w.run()
```

`data` has: `w h t`, `mouse {x y inside down}`, `stats {cpu gpu mem disk cpu_temp gpu_temp mem_used_gb mem_total_gb
net_down_kbps net_up_kbps battery charging}`, `media {title artist album playing pos dur}`,
`weather {city text temp code ok}`, `theme {primary ink ink2 card secondary tertiary dark}`.

The `ui` drawing calls take the same properties as the TOML items. Errors inside `draw` are shown on the
widget. A script that exits is restarted automatically; saving the file restarts it too.

Under the hood it is plain JSON over stdin/stdout, so any language works: print
`{"items":[{"type":"text","text":"hi","x":10,"y":10}]}` lines and read `{"type":"tick",...}` lines.

## Profile bindings

Any text in a TOML widget can use the profile: `{name}` `{status}` `{status_icon}` `{presence}` `{os}` `{wm}` `{shell}` `{uptime_long}`.

The built-in **Profile** widget (+ Add widget > Profile) is configured in `config\profile.toml`. Its rows are `icon|text|colour` entries separated by `;`. An icon can be a Material Symbols name, `os` (the Windows logo), `logo:arch` (any file in assets\logos), `exe:discord` (that app's own icon), or an image path.

IPC: `Aether.exe -s "status=text"`, `status_clear`, `status_icon=<symbol>`, `presence=online|idle|dnd|invisible|none`, `profile_edit`.
