# DOPE by Complex (The Gathering '95) — high resolution remake

A C++/OpenGL re-implementation of *DOPE*, the MS-DOS demo by Complex (code jmagic, graphics and music jugi, boxes
reward, "media labs" logo reward), first at The Gathering 1995. It runs on Linux and Windows at any resolution in
true colour, instead of 320×200 with 256 colours, with the original's motion, timing and music, and with
high-resolution redraws of the hand-drawn pictures (or the original pixels, `--original-art`).

**State:** the framework, the unpacker, the running order and the soundtrack are in place; the parts are being ported
(`../re/PORT.md` has the jobs). A part that is not ported yet shows a placeholder: its name, the music position, the
time and retraces since the part started, and the matching frame of the reference capture.

The remake reads everything from the original `DOPE.EXE` (584,133 bytes) and `DOPE.MOD` (957,334 bytes). They are
not included: they go in `dope/original/`.

| what | where | how it is used |
|------|-------|----------------|
| the loader | `DOPE.EXE`, the first of nine chained MZ executables (Tran's PMODE, a 32-bit GUS ProTracker player, CDPI) | unpacked at start-up (Rob Northen's ProPack method 1, `src/rnc.cpp`); the player's tables are read from it |
| the seven parts | the next seven executables, each RNC-packed | unpacked at start-up; pictures, palettes, meshes and tables are read from them by address (`src/exe.h`) |
| the music | `DOPE.MOD` (a 28-channel ProTracker module) | played by a port of the loader's player (`src/modplay.cpp`) through a model of the GUS GF1 (`src/gf1.cpp`) |
| the running order | CDPI, the chain loader, and each part's retrace-counted script | fixed times after the music starts (`src/timeline.cpp`) |

All addresses are `SEG:OFF` in the unpacked images (file offset = SEG × 16 + OFF), as in the notes in
[`../re/`](../re/).

## Running

The program looks for `DOPE.EXE` next to itself, in its parent folders, and in an `original` folder in any of those,
so `dope/original/DOPE.EXE` is found from the build folder; or pass `--exe FILE`. `DOPE.MOD` is read from the same
folder (or `--mod FILE`). The redraws are found the same way in an `art` folder (`dope/remake/art`); or pass
`--art DIR`.

```
dope-remake [--window] [--size 1280x960] [--start SECONDS] [--ss 2] [--loop] [--info] [--original-art]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↓/↑` seek ±30 s, `F`/`F11` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples. `--info` shows the time, music position, part and
retraces since the part's start in the title bar.

Offline: `--wav out.wav` writes the soundtrack; `--shots 10,62.5 --shotdir DIR --shotsize WxH` writes the frames
at those music times (seconds) to PNG (and `DIR/shots.txt`, the matching frames of the reference capture for
`tools/compare.py`); `--video out.mp4 --from T0 --to T1 [--fps 60] [--shotsize WxH]` renders a range with sound
through `ffmpeg`; `--bench T0,T1 --shotsize WxH` reports the GPU time per frame; `--cues` prints the running order;
`--picshots p3-nebula,p7-dope --shotdir DIR` (or `all`) draws pictures (redraw or original pixels) to PNG.

## Building

Requirements: a C++20 compiler, CMake ≥ 3.20 and SDL3. If SDL3 is not installed, CMake downloads and builds
it (3.2.24); `-DDOPE_FETCH_DEPS=ON` always does that. No other libraries are used (`stb_image` and
`stb_image_write` are included in `third_party`).

**Linux (Fedora)**

```
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel
cmake -S . -B build -G Ninja && cmake --build build
./build/dope-remake --window
```

On Fedora Atomic or without root, the same works inside a toolbox.

**Windows from Linux (MinGW cross-compile, Fedora)**

```
sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-winpthreads-static
mingw64-cmake -S . -B build-win -G Ninja && cmake --build build-win
```

This gives `build-win/dope-remake.exe`; it needs `SDL3.dll` (from the MinGW sysroot). Put `DOPE.EXE` and `DOPE.MOD`
next to them or in an `original` folder next to them, and the `art` folder next to them. (The program is called
`dope-remake` so it can never overwrite `DOPE.EXE`.)

**Windows (Visual Studio 2022)**: `cmake -S . -B build` then `cmake --build build --config Release` (SDL3 is
built from source). Only the MinGW build has been tried.

Development options: `-DDOPE_PARTS="p3;p5"` builds only some of the parts in `src/scenes`;
`-DDOPE_EXTRA_SOURCES=FILE` adds a source file from anywhere (experiments).

## How it is put together

- `src/main.cpp` options, playback loop, shots / video / bench; `src/demo.*` the framework (parts, the clocks and
  the capture's frame grid, pictures, render-target pool, placeholders); `src/timeline.cpp` the running order;
  `src/player.*` playback and the clock (the audible position of the soundtrack)
- `src/exe.*`, `src/rnc.cpp` reading and unpacking `DOPE.EXE`; `src/pictures.*` palettes and pictures: the catalog
  of the pictures in the part images, the redraws in `art/` or the original pixels through their palettes
- `src/music.*`, `src/modplay.*`, `src/gf1.*` the soundtrack, rendered at load time
- `src/dope/` the core library: `core.h` (sine tables, per-retrace motion, the label boxes, palette mixes and
  fades, palette textures); the 3D engine
- `src/scenes/` one port per part (`p1.cpp` .. `p7.cpp`)
- `art/` the redrawn pictures and how they were made (`art/README.md`); `tools/align_art.py` fits a redraw onto the
  original
- `tools/compare.py` puts remake frames next to the original's capture; `tools/test_music.cpp` checks the soundtrack
- [`../re/PORT.md`](../re/PORT.md) the conventions every part follows; `../re/` the decompiled notes (pseudo-C with
  every constant), the emulator harness and the scripts that make the reference capture

## Fidelity

Every routine was decompiled, modelled in Python and checked against the original running in an emulator (see
[`../re/README.md`](../re/README.md)); the ports are compared with a capture of the original made in that emulator.

- **Timing.** The clock is the soundtrack: music time 0 is the GUS timer overflow before the player's first tick
  (`src/music.h`). DOPE never synchronises its pictures with the music: the loader starts the player and runs the
  seven parts one after the other, and each part counts the vertical retrace (70.0863 Hz) from its own start. The
  remake starts each part where the reference capture (no disk time, 100 M instructions per second) starts it,
  and evaluates each part's retrace-counted motion continuously.
- **Pictures.** The hand-drawn pictures are shown as faithful high-resolution redraws by default (`art/README.md`
  says how each was made and how closely it fits the original); `--original-art` shows the original pixels as sharp
  squares. Palette fades are colour multiplies, so they apply to both.

Known differences:

- **The start-up** (the loader's text-mode message while the module loads) is skipped: the remake starts with the
  music. The end is the moment p7 sets text mode; the lines TERM and CDPI print in text mode afterwards are not shown.
- **Disk time.** On a real PC each part starts later by the time it takes to read and unpack it, and the pictures
  drift later against the music by an amount that depends on the machine; the remake follows the capture, which has
  none.
- **Three fades run at hardware speed.** p5's hand-over to p6's picture and the exit fades of p6 and p7 wait for the
  vertical retrace bit alone; on a VGA that is one step per retrace, in the emulator about four. The remake takes
  one retrace per step, so p6 starts 48 frames (0.68 s) and p7 74 frames (1.06 s) later against the music than in
  the capture, and the demo ends 266 frames (3.80 s) later.
- **The 3D engine** (p2, p3, p5, p6, p7; `src/dope/engine*`) transforms and projects the objects in float and
  rasterises them with MSAA, instead of whole-pixel vertices and integer spans, and interpolates the env and texture
  coordinates perspective-correctly instead of linearly on the screen. The maps are read filtered (the env maps'
  colours through the palette, the bump codes decoded to their displacement first) with the original's texel
  addressing, including the bump rows that p2, p5 and p6 skip. Where a map is smaller than a pixel (thin rings, far
  or bumpy objects) the remake averages it over the pixel; the original shows aliasing noise there (p7's speckled
  tentacle, p6's striped rings, p3's grainy bumps). A depth buffer replaces the painter's sort by face-centroid depth;
  between p2's discs and "C" it compares the original's sort key, so the "C" stays between the discs as in the
  original. Dropped as 16-bit artefacts: the back-face test that overflows on faces larger than about 181 pixels (in
  p2's and p5's close-ups the original drops big front faces and shows what is behind them), the trivial reject of
  triangles with every corner left or right of the screen, the never-drawn column 319 of clipped env-only spans.
- **p1.** The logo scrolls continuously instead of one pixel every second retrace, and its violet-to-orange
  cross-fade is continuous instead of 1/256 steps every 8 retraces with both products floored (up to 2/63 darker).
- **p2's motion blur** blends four times per retrace with the same decay per second, so trails of fast motion stay
  smooth at high resolution. The original blends once per drawn frame; in the capture's slow close-ups (46–62 drawn
  frames per second) its trails are shorter and fall apart into separate copies. The blend is a moving average of
  continuous ramp levels without the table's floor, so a still picture under heavy blur is not darkened (the original
  sits up to 256 / s ramp levels lower: 10 at s = 24, 23 at s = 11). The credits fade in to full white instead of
  stopping a few shades short. From about 136 s to the end of p2 (the close-ups of steps 150–152), the original's
  16-bit back-face test drops the near disc's big faces and shows the "C" and the stand through them; the remake
  draws the disc, which then fills most of the screen. After a seek the blur is rebuilt from black, starting far
  enough back that the history before it weighs less than 1/1000 (at most 1/255 different from playing through).
- **p3's Phong map** is the radial gradient it was painted as, fitted to the stored map at start-up and evaluated
  continuously along the palette ramp: without the ramp's 1-index steps, the paint program's irregularities (up to 2
  indices) and the hard edge of the white highlight disc. The object's mean colour stays within 0.5/255 of the
  capture's.
- **p4's fire** is computed per output pixel instead of in 160 two-pixel columns, as a float recursion (the integer
  one has a dead zone; the mean colour stays within about 3 % of the capture's), with its 15 levels interpolated
  instead of dithered and blended between its steps (35 per second). The swirl map is its polar formula (fitted to
  the stored map at start-up), without the stored map's glitches along the axes next to the centre. After a seek the
  fire is rebuilt from 150 steps back (up to 1/255 different from playing through, in a few pixels).
- **p4's landscape** is a continuous ray-cast with the camera gliding at P / 8 texels instead of 55 depth rows whose
  terrain jumps a texel every 4 retraces: the saw-tooth edges of steep slopes and the blocky nearest-texel colours
  are gone, and the far end of the terrain is fixed at the mean of its saw-tooth (40.25 texels).
- **p4's palette weights** are not floored: the one-step darkening of every colour at p4's start and at its end is
  dropped, so p4 ends on the chequerboard at the brightness of its picture (the original leaves it one 6-bit step
  darker for p5).
- **p5** shows the chequerboard at that brightness too, under the torus and in the hand-over. The BUMP glow's depth
  pulses continuously (no truncated table, no floored offsets). Each bump texel keeps one offset, as in the original
  (the patchy relief of the glow at the same scale), with the env read inside it filtered and an edge about one pixel
  wide between texels instead of hard pixel steps; where the texels are smaller than a pixel (the far torus) their
  offsets are averaged, so the original's speckle there is smoother. Red-map lookups that leave the map at the
  silhouette are clamped to its edge.
- **p6.** The cat's light map is its radial formula with a continuous shade instead of 64 levels. The 32 shadow masks
  (64 x 64 bits, switched every 4 retraces) are smoothed into distance fields and interpolated between neighbouring
  masks (centred on the 4 retraces each original mask is shown), so the ring shadows glide with smooth edges instead
  of jumping in 2-texel steps. The material morph and the fur's strength change continuously, the fur's offsets are
  interpolated: smooth streaks where the original shows blocky ones.
- **p7.** The light map is rendered at 4 x 4 samples per texel with the five spots at continuous positions (up to 1.8
  texels from the original's whole-texel ones) and the photo and spot interpolated; its level runs continuously
  through the grey ramp. Through the interpolated bumps the faint photo shows as a smooth marbling of the tentacle
  where the original shows grainy noise. The bump table's packed-word carry (codes without a column offset read one
  row up) is dropped.
