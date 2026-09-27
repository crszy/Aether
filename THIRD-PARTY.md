# Third-party notices

Aether is native C++ (Win32 + Direct3D 11 + Dear ImGui). Nothing below is linked or bundled as a
binary; where an algorithm was taken from another project it was reimplemented in C++, and the
licence notice is reproduced here because the licence requires the notice to travel with the work.

## Flow Launcher

<https://github.com/Flow-Launcher/Flow.Launcher> — MIT.

Aether's launcher scoring (`LIsWordStart`, `LAcronymScore`, `LFuzzyScore`, `LScore1`, `LMatch` in
`main.cpp`) follows the algorithm in `Flow.Launcher.Infrastructure/StringMatcher.cs`: an acronym
pass scored as a fraction of the name's initials, then a fuzzy pass scored on how early and how
tightly the query lands, with a length-difference bonus. The original is C# on .NET/WPF and could
not be compiled into this project, so it was reimplemented rather than copied.

```
MIT License

Copyright (c) 2020 Flow-Launcher

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Material Symbols

<https://github.com/google/material-design-icons> - Apache-2.0, Copyright (c) Google.

`assets/MaterialSymbols.ttf` is the Material Symbols Rounded variable font, subset to the 82 glyphs
this shell draws (245 KB, from a 15 MB original). `src/MaterialIcons.h` is the generated name ->
codepoint table. This is the same font Caelestia renders its icons from - their `MaterialIcon.qml`
drives it through the FILL / wght / GRAD variable axes - so the icon set matches by using the same
upstream source rather than by copying anything from Caelestia, which is GPL-3.0.

Apache-2.0 requires the notice to travel with the work; the full licence text is at
<https://www.apache.org/licenses/LICENSE-2.0>.

## Not used

Serpantinum (AGPL-3.0) and INDIUM (GPL-3.0) are Linux/Wayland projects; nothing from either is
present in Aether. Fluent Search publishes no source and carries no licence; nothing from it is
present either.


## Bundled assets

These ship with Aether and remain under their owners' terms:

- **Material Symbols** (`assets/MaterialSymbols*.ttf`) - Google, Apache License 2.0.
- **Google Sans Flex** (`assets/GoogleSansFlex.ttf`) - Google, SIL Open Font License 1.1.
- **Catppuccin Mocha visual styles** (`theme/`) - Catppuccin (MIT) and the styles' authors.
- **Distribution logos** (`assets/logos/`) - trademarks of their projects, used to label the OS in the fetch widget.
- **bongocat.gif, kurukuru.gif, dino.png, vfx/nebula.mp4** - belong to their creators; used as small decorative widgets.

No *Guilty Gear* assets are included: the Strive-style motion is drawn entirely from code.

## NanoSVG

<https://github.com/memononen/nanosvg> — zlib licence. `linux/nanosvg.h` and `linux/nanosvgrast.h`
are vendored unmodified (SVG icon parsing and rasterising); the licence notice is at the top of each file.
