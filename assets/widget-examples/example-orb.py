# aether: name="Shape orb (Python)" size=[0.2, 0.62]
"""
A Python dashboard widget: a Material 3 shape that morphs with CPU load, shows the playing song,
and cycles through shapes when you click it. Scroll over it to change the colour.
"""
import math
import aether

w = aether.Widget(refresh=0.05, background="card", radius=28)   # 20 fps, so the morph is smooth

state = {"shape": 0, "colour": 0}
COLOURS = ["primary", "secondary", "tertiary", "album"]


@w.draw
def draw(ui, data):
    t = data.t or 0
    cpu = (data.stats or {}).get("cpu", 0)
    a = aether.SHAPES[state["shape"] % len(aether.SHAPES)]
    b = aether.SHAPES[(state["shape"] + 1) % len(aether.SHAPES)]
    cx, cy = ui.w / 2, ui.h * 0.42
    r = min(ui.w, ui.h) * 0.30
    colour = COLOURS[state["colour"] % len(COLOURS)]

    # a soft halo that breathes, then the shape morphing toward the next one as the CPU works
    ui.shape(a, cx, cy, size=r * (1.12 + 0.04 * math.sin(t * 2)), color=colour, alpha=0.25, spin=t * 12)
    ui.shape(a, cx, cy, size=r, color=colour, morph_to=b, morph=min(1.0, cpu * 1.5), spin=t * 20, id="orb")
    ui.text(f"{cpu * 100:.0f}%", cx, cy, size=26, font="bold", align="center", valign="middle", color="on_primary")

    media = data.media or {}
    title = media.get("title") or "Nothing playing"
    ui.icon("music_note" if media.get("title") else "music_off", 26, ui.h - 44, size=20, color="ink2")
    ui.text(title, 44, ui.h - 54, size=14, font="bold", max_width=ui.w - 56)
    ui.text(media.get("artist") or "", 44, ui.h - 34, size=12, color="ink2", max_width=ui.w - 56)
    ui.text(a.replace("_", " "), ui.w / 2, 16, size=12, color="ink2", align="center")


@w.click
def click(ev, data):
    state["shape"] += 1


@w.scroll
def scroll(ev, data):
    state["colour"] += 1 if ev.dy < 0 else -1


w.run()
