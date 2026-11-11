# Paper by Psychic Link (Wired '96) — high resolution remake

A C++/OpenGL re-implementation of *Paper*, the 64k intro by Statix of Psychic Link with music by Vic of Acme,
released at Wired '96. It runs on Linux and Windows at any resolution in true colour, instead of 320×200 in
256 colours, with the original's motion, timing and music.

The remake reads everything from the original `PAPER.EXE` (63,845 bytes). It is not included: get it with
`scripts/fetch-originals.sh paper` (or `.ps1` on Windows) from the repository root, which puts it in `paper/original/`.

| what | where in the exe | how it is used |
|------|------------------|----------------|
| the program image | PKLITE 1.15 compressed | unpacked at start-up by `src/pklite.cpp` and checked against the known release |
| tune + 8 samples | pattern stream at 0x7a9b, raw 8-bit samples after it | played by a port of the intro's own player through a model of the GUS GF1 |
| font, logo art, ink image, cloud texture source, palettes | static data (0x18ed7, 0x1bf03, 0x196c3, 0x16ca3, 0x169a3, …) | glyphs rebuilt as distance fields, images upscaled, textures regenerated at high resolution |
| 3D objects, camera keys, sketch strokes, handwriting | 0x174a3, 0x1e353, 0x1878b, 0x466f, 0x5073, 0x18a2f | drawn on the GPU with a depth buffer, 4× MSAA and resolution-scaled pencil lines |
| the random generator's seed | 0x9a0 | replayed exactly, so the jitter, oscillators, canyon texture and flock are the ones the original makes |

All addresses are offsets in the intro's 32-bit segment; the notes in [`../re/`](../re/) explain every one of them.

## Running

The program looks for `PAPER.EXE` next to itself, in its parent folders, and in an `original` folder in any of
those, so `paper/original/PAPER.EXE` is found from the build folder. Or pass `--exe FILE`.

```
paper-remake [--window] [--size 1280x960] [--start SECONDS | --tick T] [--ss 2] [--loop] [--info]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↓/↑` seek ±30 s, `F`/`F11` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples. `--info` shows the tick and part in the title bar.

Offline: `--wav out.wav` writes the soundtrack; `--shots 2000,4500 --shotdir DIR --shotsize WxH` writes the
frames at those ticks to PNG; `--video out.mp4 --from T0 --to T1 [--fps 60] [--shotsize WxH]` renders a tick
range with sound through `ffmpeg`; `--bench T0,T1 --shotsize WxH` reports the GPU time per frame.

## Building

Requirements: a C++20 compiler, CMake ≥ 3.20 and SDL3. If SDL3 is not installed, CMake downloads and builds
it (3.2.24); `-DPAPER_FETCH_DEPS=ON` always does that. No other libraries are used.

**Linux (Fedora)**

```
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel
cmake -S . -B build -G Ninja && cmake --build build
./build/paper-remake --window
```

On Fedora Atomic or without root, the same works inside a toolbox.

**Windows from Linux (MinGW cross-compile, Fedora)**

```
sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-winpthreads-static
mingw64-cmake -S . -B build-win -G Ninja && cmake --build build-win
```

This gives `build-win/paper-remake.exe` and `SDL3.dll`. Copy both anywhere, with `PAPER.EXE` next to them or in an
`original` folder next to them. (The program is called `paper-remake` so it can never overwrite `PAPER.EXE`.)

**Windows (Visual Studio 2022)**: `cmake -S . -B build` then `cmake --build build --config Release` (SDL3 is
built from source). Only the MinGW build has been tried.

## How it is put together

- `src/main.cpp` options, playback loop, shots / video / bench; `src/demo.*` the framework (parts, the
  tick timeline, render-target pool); `src/timeline.cpp` the running order in 70 Hz ticks
- `src/exe.*`, `src/pklite.cpp` reading `PAPER.EXE`
- `src/music.*` the intro's tracker player (0x1ed6) and the GF1 model, rendered at load time (0.1 s); its
  audible position is the demo's clock
- `src/paper/` the core library: the random generator and its call positions (`common.h`), palettes and the
  global fade (`palette`, `fade`), font, cloud texture and paper grain, oscillators and the warp, blur, the
  page peel (`core2d.h` maps these), and the 3D engine: exact 16.16 matrices, object loaders, the transform /
  light / draw routines on the GPU and the pencil lines (`math3d`, `obj3d`, `render3d`, `pencil`)
- `src/scenes/` one port per part: `title` (title and "the end") and `title_closing`, `sketch`, `scene3d`
  (coloured scene and the first page peel), `logo`, `teal` and `flock` with the shared `planes_canyon`,
  `greetings`
- `tools/compare.py` puts remake frames next to the original's capture; `tools/test_pklite.cpp` and
  `tools/test_music.cpp` check the unpacker and the player against the reverse-engineering references
- `docs/BRIEF.md` the conventions every part follows (the brief the porting agents worked from); `../re/` the
  decompiled notes (pseudo-C with every constant), the emulator harness and the scripts that make the reference
  capture

## Fidelity

Every routine was decompiled, modelled in Python and checked against the original running in an emulator, then
ported and compared frame by frame with a capture of the original made in that emulator (see
[`../re/README.md`](../re/README.md) to make it yourself).

- **Timing.** The original advances almost everything in its 70.0018 Hz timer interrupt. The remake steps the
  same per-tick state exactly, from the start of each part, and interpolates between ticks, so motion is smooth
  at any refresh rate and seeking is exact. The clock is the audio position.
- **Music.** The tune is played by the intro's own player logic, driving a model of the GUS GF1 (volume ramps
  start from the voice's current volume, as on a real card). Its register writes and per-tick counter match the
  reference player exactly.
- **Randomness.** The intro's lagged-Fibonacci generator is replayed from its seed in the exe, with the call
  count at the start of every generator measured in the capture, so the letter jitter, the warps, the teal
  canyon texture and the flock are the original's.
- **The flock.** This is the only part that advances per drawn frame, so its speed depended on the PC. The
  remake steps it at a fixed 34 frames per second, the speed of the capture. (On fast machines the original
  skips the part altogether through a race on the palette fade; the remake always plays it.)
- **Sharper.** Evaluated per pixel instead of the original's 8×8-pixel interpolation grids (warps, canyon);
  colours in floating point instead of 32-step palette ramps; Gaussians instead of repeated box blurs;
  distance-field text; anti-aliased polygons and resolution-scaled pencil strokes; the ink fill grows smoothly
  along the original's fronts; the page peel is a continuous fold instead of 25 strips.
- **Kept on purpose.** The teal plane's shading flipping while its light vector overflows, the flock camera's
  odd motion (a bug that subtracts all three camera coordinates from one axis), the streaky decay of the
  closing "1996" glow (made by rounding in 150 blur passes, so it uses the original's integer blur, upscaled),
  the soft title letters, the handwriting without sag.

Known differences:

- The flock's planes drift up to about 8 steps from the capture late in the part: the capture's frame rate
  varied between 33 and 37 fps, the remake's step rate is fixed.
- The greetings surface is smoother than the original, whose fine streaks came from 8-bit texture sampling.
- The pencil outlines on the small flock planes are a little heavier than the original's.
- Parts are aligned with the capture to within one tick (14 ms).
