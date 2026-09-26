# tools

## slowwin.c — the render-loop stall reproducer

Builds a set of windows that are **slow but never flagged hung**, which is the case that actually
costs the shell time. A fully wedged window is cheap: Windows marks it hung after ~5 s and
`SMTO_ABORTIFHUNG` then returns instantly. The expensive window pumps just often enough to stay
off that list while spending most of its time not pumping, so every inter-thread SEND aimed at it
burns its whole timeout. A DAW mid-render, an installer, a game loading a level.

    cl /nologo /O2 tools\slowwin.c /Fe:slowwin.exe user32.lib
    slowwin.exe <seconds> <windows> [busy_ms]
    set SLOWWIN_MIN=1 && slowwin.exe 75 16 400   # minimized, as komorebi parks a hidden workspace

Measured cost of the two probes `RefreshDock` makes, against 10 of these: **1391 ms**, with
`IsHungAppWindow` false for every one of them.

### What it was written for

`stall.txt` showed `RefreshDock` with a median of 238 ms but a p95 of 3014 ms and a worst case of
18182 ms. A tight median with a tail that long is never slow code — it is timeouts being paid in
full. The dock asks every window a blocking question twice a second on the render thread, so one
quiet app was not a 50 ms hiccup, it was 50 ms x every window x for as long as it stayed quiet.

Reproduce with `--shell` (so minimized windows are parked, the path komorebi's Minimize hiding
behaviour keeps busy) and 16 minimized slow windows, 60 s:

| | before | after |
|---|---|---|
| RefreshDock stalls | 9 | 7 |
| worst | 805 ms | 166 ms |
| total | 4860 ms | 1090 ms |

The fix is the probe penalty box in `main.cpp` (`ProbeAllowed` / `ProbeFailed` / `ProbeBudget`):
a window that fails a probe is not probed again for 4 s, and one sweep may spend only 120 ms
probing before handing the rest to the next pass.
