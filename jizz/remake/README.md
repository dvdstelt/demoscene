# JIZZ by The Black Lotus (Wired '97) — high resolution remake

A C++/OpenGL re-implementation of *JIZZ*, the 64k intro by The Black Lotus (code and design Jace, music Probe,
vector engine and metaballs Nix) that won the PC 64k intro competition at Wired '97. It runs on Linux and Windows
at any resolution in true colour, instead of 320×200 in hicolor, with the original's motion, timing and music.

The remake reads everything from the original `JIZZ.EXE` (63,960 bytes). It is not included: get it with
`scripts/fetch-originals.sh jizz` (or `.ps1` on Windows) from the repository root, which puts it in `jizz/original/`.

| what | where in the exe | how it is used |
|------|------------------|----------------|
| the program image | DOS32 v3.5 "Adam" executable, compressed | unpacked at start-up by `src/adam.cpp` (the DOS32 loader's decompressor and relocations) and checked against the known release |
| the music | module part at 0x1f644, 12 synthesiser records at 0x16423, MXMPlay's frequency tables at 0x2a357 | the samples are synthesised as the intro does (`src/mp4synth.cpp`), the song is played by a port of MXMPlay (`src/mxmplay.cpp`) through a model of the GUS GF1 (`src/gf1.cpp`) |
| the textures | 16 byte-code scripts at 0x12439..0x12c32, two RLE pictures at 0x275b8 and 0x27d68 | run by a port of the texture interpreter, exactly at 256×256 and in floating point at 1024×1024 (`src/jizz/texgen*.cpp`) |
| the 3D scenes | object scripts at 0x157a4, 0x157fa, 0x15945, 0x15973, 0x159cb, camera paths at 0x15afc | built by a port of Nix's object interpreter and drawn on the GPU (`src/jizz/engine*.cpp`) |
| the timeline | the sync table at 0x6417 | the parts' sync events at the music positions the original uses |
| the rest | the "TBL" mask at 0x662b, the marching-cubes table at 0x28ed8, the random seeds, part constants | read by address where the parts need them |

All addresses are offsets in the unpacked program image; the notes in [`../re/`](../re/) explain every one of them.

## Running

The program looks for `JIZZ.EXE` next to itself, in its parent folders, and in an `original` folder in any of
those, so `jizz/original/JIZZ.EXE` is found from the build folder. Or pass `--exe FILE`.

```
jizz-remake [--window] [--size 1280x960] [--start SECONDS] [--ss 2] [--loop] [--info]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↓/↑` seek ±30 s, `F`/`F11` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples. `--info` shows the time and part in the title bar.

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
`original` folder next to them. (The program is called `jizz-remake` so it can never overwrite `JIZZ.EXE`.)

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
  camera paths, the GPU renderer)
- `src/scenes/` one port per part: `tbl`, `ribbons`, `tunnel`, `tube`, `logo`, `spiky`, `zoom`, `field`, `blobs`,
  `end`
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
- **Visibility.** A depth buffer instead of the painter's order shows the true intersection where the tube's cones
  enter the ball; faces close to the camera are clipped instead of dropped (no visible difference was found).
- **Textures.** At 1024×1024 the blobs of map10 are round instead of 8×8-texel shapes, and the carries between
  pixels of the original's dword additions are kept only where they are visible (the faint "9" in testshit).
- **TBL quirks dropped:** specks from a wrapped light-table index, and near-vertical rays the original skips.
- **Sound.** How a real GUS sums and clips 14 voices is not known; the mix is normalised to 90% of its peak.
- **Seeking into the TBL part** replays its feedback from the part start (with checkpoints), which can take
  about a second at high resolutions.
