# STARS: Wonders of the World by NoooN (1995) — high resolution remake

A C++/OpenGL re-implementation of *STARS: Wonders of the World*, the MS-DOS demo by NoooN (music Groo, 3D motion and
vertices BARTi, system and special effects KARL, script and pixels RA, sound system LCA). It runs on Linux and
Windows at any resolution in true colour, instead of 320×200 with 256 colours, with the original's motion, timing
and music, and with high-resolution redraws of RA's pictures (or the original pixels, `--original-art`).

**State:** the framework, the unpacker, the soundtrack and the running order are in place; the parts are being
ported (`../re/PORT.md` has the jobs). A part that is not ported yet shows a placeholder: its name, the music
position and the time since the part started.

The remake reads everything from the original `STARS.EXE` (1,224,140 bytes). It is not included: it goes in
`stars/original/`.

| what | where in the exe | how it is used |
|------|------------------|----------------|
| the program image | an Infiny Loader–encrypted PKLITE 1.1x executable | decrypted and decompressed at start-up by `src/unpack.cpp` (as the loader stub and PKLITE's decompressor do it) and checked against the known release |
| the data | an overlay of 58 files, 56 DIET-compressed | unpacked by `src/unpack.cpp` (DIET as 0x4ad47); pictures, palettes, maps, objects and tables are read by file number |
| the music | file 1 (a 16-channel MOD) | played by a port of LCA's MOD-Player V3.02 (`src/lcaplay.cpp`) through a model of the GUS GF1 (`src/gf1.cpp`) |
| the running order | main 0x4d | the music positions main waits for, as music times (`src/timeline.cpp`) |
| the rest | tables at image addresses (sine tables, the random seed, part constants) | read by address where the parts need them |

All addresses are offsets in the unpacked program image; the notes in [`../re/`](../re/) explain every one of them.

## Running

The program looks for `STARS.EXE` next to itself, in its parent folders, and in an `original` folder in any of
those, so `stars/original/STARS.EXE` is found from the build folder; or pass `--exe FILE`. The redraws are found
the same way in an `art` folder (`stars/remake/art`); or pass `--art DIR`.

```
stars-remake [--window] [--size 1280x960] [--start SECONDS] [--ss 2] [--loop] [--info] [--original-art] [--minute even|odd|clock]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↓/↑` seek ±30 s, `F`/`F11` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples. `--info` shows the time, music position and part in the
title bar. `--minute`: the original reads the clock at start and on odd minutes swaps two name cards of the intro and
the first two credits; the remake shows the even-minute version (as in the reference capture) unless told otherwise.

Offline: `--wav out.wav` writes the soundtrack; `--shots 10,62.5 --shotdir DIR --shotsize WxH` writes the frames
at those music times (seconds) to PNG; `--video out.mp4 --from T0 --to T1 [--fps 60] [--shotsize WxH]` renders a
range with sound through `ffmpeg`; `--bench T0,T1 --shotsize WxH` reports the GPU time per frame; `--cues` prints the
running order; `--picshots 57,5 --shotdir DIR` draws pictures (redraw or original pixels) to PNG.

## Building

Requirements: a C++20 compiler, CMake ≥ 3.20 and SDL3. If SDL3 is not installed, CMake downloads and builds
it (3.2.24); `-DSTARS_FETCH_DEPS=ON` always does that. No other libraries are used (`stb_image` and
`stb_image_write` are included in `third_party`).

**Linux (Fedora)**

```
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel
cmake -S . -B build -G Ninja && cmake --build build
./build/stars-remake --window
```

On Fedora Atomic or without root, the same works inside a toolbox.

**Windows from Linux (MinGW cross-compile, Fedora)**

```
sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-winpthreads-static
mingw64-cmake -S . -B build-win -G Ninja && cmake --build build-win
```

This gives `build-win/stars-remake.exe`; it needs `SDL3.dll` (from the MinGW sysroot). Put `STARS.EXE` next to them
or in an `original` folder next to them, and the `art` folder next to them. (The program is called `stars-remake` so
it can never overwrite `STARS.EXE`.)

**Windows (Visual Studio 2022)**: `cmake -S . -B build` then `cmake --build build --config Release` (SDL3 is
built from source). Only the MinGW build has been tried.

`-DSTARS_PARTS="intro;cloud"` builds only some of the parts in `src/scenes` (development).

## How it is put together

- `src/main.cpp` options, playback loop, shots / video / bench; `src/demo.*` the framework (parts, the clocks,
  pictures, render-target pool, placeholders); `src/timeline.cpp` the running order; `src/player.*` playback and
  the clock (the audible position of the soundtrack)
- `src/exe.*`, `src/unpack.cpp` reading `STARS.EXE`; `src/pictures.*` palettes and pictures: the redraws in `art/`
  or the original pixels through their palettes
- `src/music.*`, `src/lcaplay.*`, `src/gf1.*` the soundtrack, rendered at load time
- `src/stars/` the core library: `core.h` (the random generator 0x4af10, the sine tables, the palette fader,
  palette textures); the 3D engine
- `src/scenes/` one port per part
- `art/` the redrawn pictures and how they were made (`art/README.md`); `tools/align_art.py` fits a redraw onto the
  original
- `tools/compare.py` puts remake frames next to the original's capture; `tools/test_music.cpp` checks the soundtrack
- [`../re/PORT.md`](../re/PORT.md) the conventions every part follows; `../re/` the decompiled notes (pseudo-C with
  every constant), the emulator harness and the scripts that make the reference capture

## Fidelity

Every routine was decompiled, modelled in Python and checked against the original running in an emulator (see
[`../re/README.md`](../re/README.md)); the ports are compared with a capture of the original made in that emulator.

- **Timing.** The clock is the music: music time 0 is the moment the original's player starts the GUS timers, and
  the music runs on the GUS timer periods the player programs (1.17 % / 1.39 % faster than the module's nominal
  tempo). Main's script waits for music positions; the remake starts each part at the same position, or, where the
  original's set-up routines took longer than that, at the frame where the capture (a 486DX2/66-class machine)
  starts it. The parts' animation is stepped by the PIT at 70.0635 Hz and is evaluated continuously; what the
  original advances once per drawn frame is stepped at the capture's frame rate and blended between steps.
- **Pictures.** The hand-drawn pictures are shown as faithful high-resolution redraws by default (`art/README.md`
  says how each was made and how closely it fits the original); `--original-art` shows the original pixels as sharp
  squares. Palette fades are colour multiplies, so they apply to both.

Known differences:

- **The start-up** (PMODE's messages, loading and the precalc, about 4 s of black) is skipped: the remake starts
  with the music.
- **Part ends** that the original tests once per drawn frame come up to one drawn frame later there; the remake ends
  on the tick or row.
- **What a part shows at t.** The original draws a frame from the state at the moment it starts drawing and shows it
  one or two retraces later (the warp and the tunnel draw at 35 fps, straight into video memory, so the capture also
  shows half old, half new frames); the remake shows the state at t. Against the capture the warp and the tunnel are
  30–45 ms ahead.
- **Intro.** The flower texture, the petal outline and the radial offset are evaluated per pixel instead of along
  4096 integer rays; the band brightness and the card fade are continuous instead of 8-bit products of 6-bit
  colours. The band phases run on from card to card by the number of drawn frames, which depends on where in a frame
  each card starts: the remake counts with the average, so a card can be one drawn frame (1/41 of a pulse) off.
- **Warp.** Blocks 2–4 start on the tick at which the block before ends; the capture starts each up to one drawn
  frame later (about 1 tick each). The patch is evaluated in floats (the original's packed 8.8 sums are off by up to
  3.6 texels) and the pictures are read with a filter. Picture 13 has no redraw and shows its original pixels in both
  modes. Dropped: the tearing and the race between the PIT and the curve routine (62 of 915 frames mix two phases).
- **Tunnel.** The maps are read bilinearly at the continuous hit depth, so near the camera, where one map column
  covers about 10 original pixels, the original's flat streaks become smooth gradients and sharp steps in the colour
  map (the darker band at columns 240–0) become soft. The forward motion is continuous (the original moves in whole
  texels). Kept: steps 0 and 1 read the same column, step 0 without the twist.
- **3D engine** (`src/stars/engine.h`; bee, spike ball, wall, star, torso). Vertices are projected in float with
  exact sin / cos (the original's 1023-amplitude table makes objects 0.1–0.3 % smaller) and rasterised with MSAA
  instead of integer corners; texture coordinates and normals are interpolated perspective-correctly, and the env
  coordinate is evaluated per pixel from the interpolated normal instead of interpolating per-vertex coordinates
  (rounder highlights on coarse meshes). The light maps are the light curve at the continuous radius with continuous
  levels instead of 16 / 32 isqrt rings, the bump offsets are filtered, and the texture's colours are filtered in
  true colour, so the original's texel noise on the spike ball and the wall is smoother. Faces that come nearer than
  2F to the eye are clipped at that plane instead of disappearing whole (visible as the star flies into the camera).
  Where the parts use a depth buffer the order of faces is the geometric one instead of the painter's sort; the bee
  keeps the original's file order, because its objects overlap themselves.
