"""
aether.py - write Aether dashboard widgets in Python.

A Python widget is a normal script. Aether runs it with `python -u your_widget.py`, sends it JSON
lines on stdin (ticks, clicks, scrolls) and draws whatever it prints on stdout. This module hides that:

    import aether

    w = aether.Widget(refresh=1.0, background="card")

    @w.draw
    def draw(ui, data):
        ui.shape("cookie12", ui.w / 2, ui.h / 2, size=ui.h * 0.4, color="primary")
        ui.text(f"{data.stats.cpu * 100:.0f}%", ui.w / 2, ui.h / 2, size=28,
                align="center", valign="middle", color="on_primary", font="bold")

    @w.click
    def click(ev, data):
        aether.run("notepad.exe")

    w.run()

Coordinates are pixels from the widget's top-left; ui.w / ui.h are its current size.
Colours are theme roles ("primary", "secondary", "tertiary", "ink", "ink2", "card", "on_primary",
"album", "error") or "#rrggbb" / "#rrggbbaa". Shapes are Material 3 Expressive names - see SHAPES.

Every drawing call accepts extra keyword properties, e.g. alpha=0.5, on_click="shell:lock",
id="play" (ids come back in click events so you know what was pressed).

Put a comment near the top of your script to name it in the widget palette:
    # aether: name="My widget" size=[0.25, 0.4]
(size is a fraction of the dashboard's content area.)
"""
import json
import sys
import threading
import time
import subprocess
import os

SHAPES = [
    "circle", "square", "slanted", "oval", "pill", "triangle", "arrow", "diamond", "pentagon", "gem",
    "very_sunny", "sunny", "cookie4", "cookie6", "cookie7", "cookie9", "cookie12", "clover4",
    "soft_burst", "ghostish", "arch", "fan", "semicircle", "clamshell", "clover8", "burst", "boom",
    "soft_boom", "flower", "puffy", "puffy_diamond", "pixel_circle", "pixel_triangle", "bun", "heart",
]


class _Obj(dict):
    """dict with attribute access: data.stats.cpu"""
    def __getattr__(self, k):
        v = self.get(k)
        return _Obj(v) if isinstance(v, dict) else v


class UI:
    def __init__(self, w, h):
        self.w = w
        self.h = h
        self.items = []

    def _add(self, kind, **props):
        props = {k: v for k, v in props.items() if v is not None}
        props["type"] = kind
        self.items.append(props)
        return props

    # ---- primitives ----
    def text(self, text, x, y, size=15, color="ink", align="left", valign="top", font="regular",
             max_width=None, wrap=False, **kw):
        return self._add("text", text=str(text), x=x, y=y, size=size, color=color, align=align,
                         valign=valign, font=font, max_width=max_width, wrap=wrap, **kw)

    def rect(self, x, y, w, h, color="card", radius=0, **kw):
        return self._add("rect", x=x, y=y, w=w, h=h, color=color, radius=radius, **kw)

    def circle(self, x, y, radius, color="primary", thickness=0, **kw):
        return self._add("circle", x=x, y=y, radius=radius, color=color, thickness=thickness, **kw)

    def shape(self, shape, x, y, size=30, color="primary", morph_to=None, morph=0, spin=0, image=None, **kw):
        """size is the radius. morph (0..1) blends toward morph_to. spin is degrees."""
        return self._add("shape", shape=shape, x=x, y=y, size=size, color=color, morph_to=morph_to,
                         morph=morph, spin=spin, image=image, **kw)

    def ring(self, x, y, radius, value, thickness=6, color="primary", track=None, start=-90, sweep=360, **kw):
        return self._add("ring", x=x, y=y, radius=radius, value=value, thickness=thickness, color=color,
                         track=track, start=start, sweep=sweep, **kw)

    def bar(self, x, y, w, h, value, color="primary", track=None, radius=None, vertical=False, **kw):
        return self._add("bar", x=x, y=y, w=w, h=h, value=value, color=color, track=track, radius=radius,
                         vertical=vertical, **kw)

    def image(self, path, x, y, w, h, radius=0, fit="cover", **kw):
        """PNG, JPG, WEBP or an animated GIF."""
        return self._add("image", path=path, x=x, y=y, w=w, h=h, radius=radius, fit=fit, **kw)

    def icon(self, name, x, y, size=24, color="ink", **kw):
        """Any Material Symbols name: "music_note", "bolt", "favorite"..."""
        return self._add("icon", name=name, x=x, y=y, size=size, color=color, **kw)

    def line(self, x, y, x2, y2, color="ink2", thickness=1.5, **kw):
        return self._add("line", x=x, y=y, x2=x2, y2=y2, color=color, thickness=thickness, **kw)

    def graph(self, source, x, y, w, h, color="primary", fill=True, **kw):
        """source: cpu | gpu | mem | disk | net (Aether's own history)"""
        return self._add("graph", source=source, x=x, y=y, w=w, h=h, color=color, fill=fill, **kw)

    def button(self, x, y, w, h, text=None, icon=None, color="card2", hover_color=None, radius=12,
               id=None, on_click=None, **kw):
        return self._add("button", x=x, y=y, w=w, h=h, text=text, icon=icon, color=color,
                         hover_color=hover_color, radius=radius, id=id, on_click=on_click, **kw)


class Widget:
    def __init__(self, refresh=1.0, background="card", shape=None, image=None, color=None, radius=None,
                 padding=None):
        self.refresh = refresh
        self._cfg = {"refresh_ms": int(refresh * 1000), "background": background}
        for k, v in (("shape", shape), ("image", image), ("color", color), ("radius", radius), ("padding", padding)):
            if v is not None:
                self._cfg[k] = v
        self._draw = None
        self._click = None
        self._scroll = None
        self.data = _Obj({"w": 200, "h": 120, "stats": {}, "media": {}, "weather": {}, "theme": {}, "mouse": {}})
        self.state = {}
        self._lock = threading.Lock()

    # decorators
    def draw(self, fn):
        self._draw = fn
        return fn

    def click(self, fn):
        self._click = fn
        return fn

    def scroll(self, fn):
        self._scroll = fn
        return fn

    def redraw(self):
        if not self._draw:
            return
        d = self.data
        ui = UI(float(d.get("w", 200)), float(d.get("h", 120)))
        try:
            self._draw(ui, d)
        except Exception as e:  # show the error on the widget instead of dying
            ui.items = [{"type": "text", "text": f"{type(e).__name__}: {e}", "x": 12, "y": 12, "size": 13,
                         "color": "error", "wrap": True, "max_width": ui.w - 24}]
        _send({"items": ui.items})

    def run(self):
        _send({"config": self._cfg})
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                ev = json.loads(line)
            except ValueError:
                continue
            kind = ev.get("type")
            if kind in ("init", "tick"):
                self.data.update(ev)
                self.redraw()
            elif kind == "click" and self._click:
                try:
                    self._click(_Obj(ev), self.data)
                except Exception as e:
                    log(f"click handler: {e}")
                self.redraw()
            elif kind == "scroll" and self._scroll:
                try:
                    self._scroll(_Obj(ev), self.data)
                except Exception as e:
                    log(f"scroll handler: {e}")
                self.redraw()


def _send(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def log(text):
    """Appears under the widget while it has nothing drawn, and on stderr for debugging."""
    _send({"log": str(text)})


def run(command):
    """Start a program without waiting for it."""
    subprocess.Popen(command, shell=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))


def shell(command):
    """Send an Aether command: "lock", "launcher", "dashboard=Media", "wallpaper=live"..."""
    exe = os.path.join(os.path.dirname(os.path.dirname(os.environ.get("AETHER_WIDGETS", "").rstrip("\\/"))), "Aether.exe")
    subprocess.Popen([exe, "-s", command], creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
