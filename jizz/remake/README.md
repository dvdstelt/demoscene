# JIZZ by The Black Lotus (Wired '97) — high resolution remake

A C++/OpenGL re-implementation of *JIZZ*, the 64k intro by The Black Lotus (code and design Jace, music Probe,
vector engine and metaballs Nix) that won the PC 64k intro competition at Wired '97. It runs on Linux and Windows
at any resolution in true colour, instead of 320×200 in hicolor, with the original's motion, timing and music, and
with high-resolution redraws of its two pictures, Sjaak's "jizz" logo and Jace's "~JIZZ~ TBL'97" (or the original
pixels, `--original-art`).

The remake reads everything from the original `JIZZ.EXE` (63,960 bytes), in the original release in [`jizz/original/`](../original/).

| what | where in the exe | how it is used |
|------|------------------|----------------|
| the program image | DOS32 v3.5 "Adam" executable, compressed | unpacked at start-up by `src/adam.cpp` (the DOS32 loader's decompressor and relocations) and checked against the known release |
| the music | module part at 0x1f644, 12 synthesiser records at 0x16423, MXMPlay's frequency tables at 0x2a357 | the samples are synthesised as the intro does (`src/mp4synth.cpp`), the song is played by a port of MXMPlay (`src/mxmplay.cpp`) through a model of the GUS GF1 (`src/gf1.cpp`) |
| the textures | 16 byte-code scripts at 0x12439..0x12c32, two RLE pictures at 0x275b8 and 0x27d68 | run by a port of the texture interpreter, exactly at 256×256 and in floating point at 1024×1024 (`src/jizz/texgen*.cpp`); the two pictures redrawn (`art/`) where they are shown |
| the 3D scenes | object scripts at 0x157a4, 0x157fa, 0x15945, 0x15973, 0x159cb, camera paths at 0x15afc | built by a port of Nix's object interpreter and drawn on the GPU (`src/jizz/engine*.cpp`) |
| the timeline | the sync table at 0x6417 | the parts' sync events at the music positions the original uses |
| the rest | the "TBL" mask at 0x662b, the marching-cubes table at 0x28ed8, the random seeds, part constants | read by address where the parts need them |

All addresses are offsets in the unpacked program image; the notes in [`../re/`](../re/) explain every one of them.

## Running

The program looks for `JIZZ.EXE` next to itself, in its parent folders, and in an `original` folder in any of
those, so `jizz/original/JIZZ.EXE` is found from the build folder. Or pass `--exe FILE`. The redraws are found the
same way in an `art` folder (`jizz/remake/art`); or pass `--art DIR`.

```
jizz-remake [--window] [--size 1280x960] [--start SECONDS] [--ss 2] [--loop] [--info] [--original-art]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↓/↑` seek ±30 s, `F`/`F11` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples. `--info` shows the time and part in the title bar.
`--original-art` shows the original pixels of the "jizz" logo and the end picture instead of the redraws.

Offline: `--wav out.wav` writes the soundtrack; `--shots 10,62.5 --shotdir DIR --shotsize WxH` writes the frames
at those music times (seconds) to PNG; `--video out.mp4 --from T0 --to T1 [--fps 60] [--shotsize WxH]` renders a
range with sound through `ffmpeg`; `--bench T0,T1 --shotsize WxH` reports the GPU time per frame.

## Building

Requirements: a C++20 compiler, CMake ≥ 3.20 and SDL3. If SDL3 is not installed, CMake downloads and builds
it (3.2.24); `-DJIZZ_FETCH_DEPS=ON` always does that. No other libraries are used.

**Linux (Fedora)**

```
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel
cmake -S . -B build -G Ninja && cmake --build build
./build/jizz-remake --window
```

On Fedora Atomic or without root, the same works inside a toolbox.

**Windows from Linux (MinGW cross-compile, Fedora)**

```
sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-winpthreads-static
mingw64-cmake -S . -B build-win -G Ninja && cmake --build build-win
```

This gives `build-win/jizz-remake.exe` and `SDL3.dll`. Copy both anywhere, with `JIZZ.EXE` next to them or in an
`original` folder next to them, and the `art` folder next to them. (The program is called `jizz-remake` so it can
never overwrite `JIZZ.EXE`.)

**Windows (Visual Studio 2022)**: `cmake -S . -B build` then `cmake --build build --config Release` (SDL3 is
built from source). Only the MinGW build has been tried.

## How it is put together

- `src/main.cpp` options, playback loop, shots / video / bench; `src/demo.*` the framework (parts, the
  timeline in music time, the sync events, render-target pool); `src/timeline.cpp` the running order
- `src/exe.*`, `src/adam.cpp` reading `JIZZ.EXE`
- `src/music.*` playback and the clock; `src/mp4synth.*` the module and its synthesised samples (0x1632b);
  `src/mxmplay.*` MXMPlay (0x296dc..0x2a316); `src/gf1.*` the GF1 model. The soundtrack is rendered at load time
  (0.2 s); its audible position is the demo's clock
- `src/jizz/` the core library: `core.h` (the random generator 0x141f6, spot and polar maths, the engine's colour
  tables and the 03:10 inversion, fades, thick lines, jittered quads, inset frames, the glow lines), `textures.h`
  and `texgen*` (the texture generator 0x12c50 / 0x12e72), `engine.h` (Nix's vector engine: object scripts,
  camera paths, the GPU renderer), `art.h` (the redrawn pictures)
- `src/scenes/` one port per part: `tbl`, `ribbons`, `tunnel`, `tube`, `logo`, `spiky`, `zoom`, `field`, `blobs`,
  `end`
- `art/` the redrawn pictures and how they were made (`art/README.md`); `tools/align_art.py` makes the picture to
  upload, fits a redraw onto the original and writes the comparison sheet
- `tools/compare.py` puts remake frames next to the original's capture; `tools/test_music.cpp` checks the
  module, the GUS register stream, the GUS memory and the music positions against the capture; `tools/texdump.cpp`
  with `check_textures.py` checks the textures; `tools/enginetest/` checks the engine against dumped renders;
  `tools/coretest.*` the core library
- [`../re/PORT.md`](../re/PORT.md) the conventions every part follows; `../re/` the decompiled notes (pseudo-C with
  every constant), the emulator harness and the scripts that make the reference capture

## Fidelity

Every routine was decompiled, modelled in Python and checked against the original running in an emulator, then
ported and compared with a capture of the original made in that emulator (see [`../re/README.md`](../re/README.md)
to make it yourself).

- **Timing.** The original animates from MXMPlay's timer (PIT clocks since the music started, interpolated), not
  from a fixed tick. The remake evaluates the same formulas with the continuous music time, so motion is smooth at
  any refresh rate and seeking is exact. The clock is the audio position.
- **Music.** The module is built byte for byte as the intro builds it, including the synthesised samples (their
  bytes depend on the x87's 24-bit precision, which the port reproduces in portable C++). The song is played by
  MXMPlay's own logic; its GUS register writes, GUS memory and music positions match the capture exactly. The GF1
  model ramps volumes from the voice's current volume, as a real card does.
- **Frame-rate dependent state.** A few things advance once per drawn frame: the TBL part's fades, colour pulse
  and feedback, the 03:10 inversion, the glow lines, rings and rectangle jitter of the ribbon and tunnel parts, and
  the field lines' starting speed. The remake steps them at the frame rate the capture shows in that phase (the
  TBL part on the capture's own frame clock) and blends between steps.
- **Randomness.** The intro's random generator is replayed from its seeds, so the textures, the samples, the
  sparks and the jittered rectangles are the original's.
- **Sharper.** Effects are evaluated per pixel (polar maps, warps, field lines, light rays); textures are made by
  the original's scripts at 1024×1024 in floating point where that matches the exact 256×256 result; the 3D
  scenes are drawn with a depth buffer, 4× MSAA, perspective-correct filtered textures and a per-pixel highlight;
  lines are anti-aliased strokes of the original's width; 2×2 box blurs are Gaussians of the same variance;
  colours, fades and colour tables are continuous instead of 5- and 6-bit.
- **Pictures.** The "jizz" logo and the end picture are shown as faithful high-resolution redraws by default (see
  below); `--original-art` shows the original pictures, filtered up to 1024×1024 as part of their textures.

Known differences:

- **Part starts and latency.** The original enters a part at the first frame after the music reaches its row and
  shows each frame one or two frames after it read the timer. The remake starts on the row and shows the state at
  the current time, so its motion runs up to about 0.1 s ahead of the capture.
- **The text-mode precalc** (24 s of "extracting music", "decrunching gfx") is skipped; the remake starts with the
  music.
- **Glow lines and the TBL trail** are computed in floating point; the original's integer truncation is replaced
  by its average effect. The TBL part's few frames of leftover glow at the start of the ribbon part are not shown.
- **The rings** of the ribbon part are true ellipse arcs instead of 20- and 30-sided polygons.
- **The metaballs** are ray-marched as the exact surface the original polygonises on a 64-unit grid (the sum of
  1/distance to the six balls equals 1/84), with per-pixel normals. The original's vertices lie on that surface,
  so the shapes and the moments the balls merge are the same, without the facets. One timer drives the balls and
  the camera (the original reads them 12–14 ms apart).
- **Fades** are continuous: the first frame of a fade from white is white, where the original's first of 32 steps
  already shows 1/32 of the picture.
- **The jizz logo's outline** is a smooth one-pixel-wide dilation of the logo's silhouette.
- **The redrawn pictures** have smooth edges and sharp points where the originals have pixel steps and grey
  anti-aliasing. The logo has 4.8 % less white than the original, a thin band along the edges, and the stroke at
  its lower right has three blades and one thin streak where the original has four or five thin streaks; the end
  picture's letters are 10 % heavier. Thin dark gaps inside the logo's shapes are shown dark, like the outline.
  [`art/README.md`](art/README.md) has the measured fit.
- **Visibility.** A depth buffer instead of the painter's order shows the true intersection where the tube's cones
  enter the ball; faces close to the camera are clipped instead of dropped (no visible difference was found).
- **Textures.** At 1024×1024 the blobs of map10 are round instead of 8×8-texel shapes, and the carries between
  pixels of the original's dword additions are kept only where they are visible (the faint "9" in testshit).
- **TBL quirks dropped:** specks from a wrapped light-table index, and near-vertical rays the original skips.
- **Sound.** How a real GUS sums and clips 14 voices is not known; the mix is normalised to 90% of its peak.
- **Seeking into the TBL part** replays its feedback from the part start (with checkpoints), which can take
  about a second at high resolutions.

## The redrawn pictures

JIZZ shows two hand-drawn pictures, both 4-bit greyscale run-length bitmaps (256×200, op 0x22 of the texture
scripts): Sjaak's "jizz" logo at 0x27d68 and Jace's "~JIZZ~ TBL'97" at 0x275b8. Texture script 14 multiplies each
with blurred clouds; the pastel colours come from those clouds. logo2 (clouds × logo) is shown by the "jizz" part
with a one-pixel dark outline, logo1 (clouds × lettering) is the end picture. At 2880×2160 one original pixel is 9
output pixels wide, more than the 1024×1024 textures hold, so with the redraws the parts do the multiply
themselves:

- `art/logo.png` and `art/lettering.png` are AI redraws made with ChatGPT's image generation from the original
  pixels and fitted onto them ([`art/README.md`](art/README.md) has the prompts, the attempts and the fit). Both are
  coverage masks over the 256×200 picture.
- The texture generator keeps the script's clouds as their own texture (`Tex::LogoClouds`, the script's W just
  before its first multiply, built at 1024×1024 and checked against the exact one like the others). The parts draw
  clouds × the picture's solid level (the maximum of the decoded picture: 255 and 238) × the redraw's shape, its
  coverage at 1/2 anti-aliased at the output resolution, so the edges stay one output pixel wide at any size. The
  multiply's floor is kept as its mean, as in the high-resolution textures.
- The "jizz" part's outline (0x1216f..0x121b9 marks the black pixels next to the picture's non-zero ones) comes from
  a distance field of the redraw's shape at the redraw's own resolution: the picture is opaque within one original
  pixel of the shape, dark between the shape and that edge, both edges anti-aliased at the output resolution.
- The TBL part's letter mask (texture script 13, testshit) is built from the lettering too. It is a generated
  texture (the picture cut to a window, warped, enlarged, blurred and inverted), so it keeps the original picture.

With `--original-art` (or without the `art` folder) the parts draw logo1 and logo2 and the outline from the
original pictures as before; those frames are the same as before the redraws, bit for bit.
