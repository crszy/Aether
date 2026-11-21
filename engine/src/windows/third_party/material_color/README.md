# material-color-utilities (HCT subset)

From https://github.com/material-foundation/material-color-utilities, `cpp/` at commit
`5b3618b16fdc3825e21d5679bafd144662088ea1` (2026-08-21). Apache License 2.0, see `LICENSE`.

Only what `Hct` needs: `cpp/cam/{hct,hct_solver,cam}.{h,cc}`, `cpp/cam/viewing_conditions.h` and
`cpp/utils/utils.{h,cc}`. Used by `system/terminal_colors_math.cpp`, the native port of the terminal
part of ii's `scripts/colors/generate_colors_material.py`. That script runs on the Python port of
this library (`materialyoucolor`), so the copy here is adjusted to give the same doubles:

- `viewing_conditions.h`: the default viewing conditions at full precision, as materialyoucolor
  computes them (upstream rounds them to 9 decimals, which moved chroma by up to 1e-3);
  `viewing_conditions.cc` (CreateViewingConditions) isn't vendored.
- `utils.cc`: `Delinearized` rounds halves to even like Python's `round()`; `YFromLstar` and
  `LstarFromY` take the L\*a\*b\* f / f^-1 path the other ports take for L\* <= 8.
- Build fixes: no abseil (`HexFromArgb` dropped), `Vec3{...}` instead of C compound literals, and
  `fabs` instead of `abs` on doubles (MSVC's headers don't always declare `abs(double)` there, and
  `abs(int)` would truncate).

Each changed file says so in its header. The import itself is the first commit touching this
directory, unmodified, so `git diff` against it shows every change.

Checked with `tools/terminal-colors-test` in the ii-windows repo: `Hct(argb)` is bit-identical to
materialyoucolor 2.0.10 for all 16,777,216 sRGB colors, `SolveToInt` agrees on 2,000,000 random
inputs, and the terminal colors match the script hex for hex.
