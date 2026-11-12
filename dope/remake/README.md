# DOPE by Complex (The Gathering '95) — high resolution remake

A C++/OpenGL re-implementation of *DOPE*, the MS-DOS demo by Complex (code jmagic, graphics and music jugi, routine
boxes sketched by jugi and pixelled by Reward, the Media Labs logo by Reward, 3D objects by jugi and M. Hakulinen)
that won the PC demo competition at The Gathering '95. It runs on Linux and Windows at any resolution in true colour,
instead of 320×200 with 256 colours, with the original's motion, timing and music, and with high-resolution redraws
of the hand-drawn pictures (or the original pixels, `--original-art`).

The remake reads everything from the original `DOPE.EXE` (584,133 bytes) and `DOPE.MOD` (957,334 bytes). They are
not included: unpack `clx_dope.zip` from scene.org into `dope/original/` (see
[`../original/README.md`](../original/)).

| what | where | how it is used |
|------|-------|----------------|
| the loader | `DOPE.EXE`, the first of nine chained MZ executables (Tran's PMODE, a 32-bit GUS ProTracker player, CDPI) | unpacked at start-up (Rob Northen's ProPack method 1, `src/rnc.cpp`) and checked by hash; the player's tables are read from it |
| the seven parts | the next seven executables, each RNC-packed | unpacked at start-up and checked by hash; pictures, palettes, meshes, object records, bump and shade tables and the parts' scripts are read from them by address (`src/exe.h`) |
| the music | `DOPE.MOD` (a 28-channel ProTracker module) | played by a port of the loader's player (`src/modplay.cpp`) through a model of the GUS GF1 (`src/gf1.cpp`), rendered at start-up |
| the running order | CDPI and each part's retrace-counted script | fixed times after the music starts, from the reference capture (`src/timeline.cpp`) |

All addresses are `SEG:OFF` in the unpacked images (file offset = SEG × 16 + OFF), as in the notes in
[`../re/`](../re/).

## Running

The program looks for `DOPE.EXE` next to itself, in its parent folders, and in an `original` folder in any of those,
so `dope/original/DOPE.EXE` is found from the build folder; or pass `--exe FILE`. `DOPE.MOD` is read from the same
folder (or `--mod FILE`). The redraws are found the same way in an `art` folder (`dope/remake/art`); or pass
`--art DIR`.

```
dope-remake [--window] [--size 1280x960] [--start SECONDS] [--ss 2] [--loop] [--info] [--original-art] [--novsync]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↓/↑` seek ±30 s, `F`/`F11` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples. `--info` shows the time, music position, part and
retraces since the part's start in the title bar. `--original-art` shows the original pixels of the pictures
instead of the redraws.

Offline: `--wav out.wav` writes the soundtrack (48 kHz stereo); `--shots 10,62.5 --shotdir DIR --shotsize WxH`
writes the frames at those music times (seconds) to PNG (and `DIR/shots.txt`, the matching frames of the reference
capture for `tools/compare.py`); `--video out.mp4 --from T0 --to T1 [--fps 60] [--shotsize WxH]` renders a range
with sound through `ffmpeg`; `--bench T0,T1 --shotsize WxH [--fps F]` reports the GPU time per frame (with `--fps`
at the steps of playback at F frames per second); `--cues` prints the running order with the capture's frames;
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

`-DDOPE_PARTS="p3;p5"` builds only some of the parts in `src/scenes` (development); the others show a placeholder
with the part's name, the music position, the time and retraces since the part started and the matching frame of
the reference capture. `-DDOPE_EXTRA_SOURCES=FILE` adds a source file from anywhere (experiments).

## How it is put together

- `src/main.cpp` options, playback loop, shots / video / bench; `src/demo.*` the framework (parts, the clocks and
  the capture's frame grid, pictures, render-target pool, placeholders); `src/timeline.cpp` the running order;
  `src/player.*` playback and the clock (the audible position of the soundtrack)
- `src/exe.*`, `src/rnc.cpp` reading and unpacking `DOPE.EXE`; `src/pictures.*` palettes and pictures: the catalog
  of the pictures in the part images, the redraws in `art/` or the original pixels through their palettes
- `src/music.*`, `src/modplay.*`, `src/gf1.*` the soundtrack, rendered at start-up
- `src/dope/` the core library: `core.h` (sine tables, per-retrace motion, the label boxes, palette mixes and
  fades, palette textures); `engine.h` (the 3D engine of p2, p3, p5, p6 and p7 on the GPU, with each copy's
  differences as options)
- `src/scenes/` one port per part: `p1` (the logo), `p2` with `p2_blur` (the credits, the "C", the motion blur),
  `p3` and `p5` with `p3p5.h` (the Phong object; the env- and bump-mapped torus), `p4` with `p4_fire` and
  `p4_land` (the fire clouds and the fractal landscape), `p6` (the cat), `p7` (the tentacle and "dope")
- `art/` the redrawn pictures and how they were made (`art/README.md`); `tools/align_art.py` fits a redraw onto the
  original
- `tools/compare.py` puts remake frames next to the original's capture; `tools/test_music.cpp` checks the soundtrack
  (`cmake --build build --target test_music`); `tools/enginetest/` checks the 3D engine against frames of the
  bit-exact model of the original's engine and renders test scenes
- [`../re/PORT.md`](../re/PORT.md) the conventions every part follows; `../re/` the decompiled notes (pseudo-C with
  every constant), the emulator harness and the scripts that make the reference capture

## Fidelity

Every routine was decompiled, modelled in Python and checked against the original running in an emulator (see
[`../re/README.md`](../re/README.md)); the ports are compared with a capture of the original made in that emulator,
which runs the demo at 100 million instructions per second, about a fast Pentium.

- **Timing.** The clock is the music: music time 0 is the GUS timer overflow after which the loader starts the
  player (`src/music.h`). DOPE never synchronises its pictures with the music: the loader starts the player and CDPI
  runs the seven parts one after the other, each counting the vertical retrace (70.0863 Hz) from its own start. The
  remake starts each part where the reference capture starts it (no disk time) and counts retraces from there.
  Between parts the screen keeps the last frame where the original's does (p3's nebula, p4's chequerboard, p5's
  swirl) and is black elsewhere.
- **What a frame shows.** Measured in the capture to the retrace (the labels' slides, the motion): p3, p5, p6 and
  p7 show the 3D state after r retraces and the label after r + 1; p2, whose frame copy blends 64,000 pixels before
  it copies, the state after r − 1 and the label after r; p1 the scroll after r + 1 (its handler changes the
  screen), p4 the state after r. `--cues` prints every part's start and end with the capture's frames.
- **Motion.** The parts' per-retrace routines are sums of per-retrace steps whose rates the scripts change at known
  retraces (the scripts are decoded from the part images at start-up). The remake evaluates them in closed form at
  the continuous retrace count, exact at whole retraces and linear between them, with exact sine and cosine instead
  of the 2048-entry tables; what the scripts set at once stays on its retrace.
- **Three fades at hardware speed.** p5's hand-over to p6's picture and the exit fades of p6 and p7 wait for the
  retrace bit alone: one step per retrace on a VGA, about four in the emulator. The remake takes one retrace per
  step, so p6 starts 48 frames (0.68 s) and p7 74 frames (1.06 s) later against the music than in the capture, and
  the demo ends 266 frames (3.80 s) later. The music loses a tick where each part calibrates its timer; those losses
  move with p6 and p7.
- **Music.** The player's GUS register writes, the GUS memory after the upload and the music position after every
  timer interrupt match the capture exactly (`tools/test_music.cpp`). The GF1 model evaluates the voices at the output
  rate (48 kHz) instead of the card's 22,050 frames per second with 28 voices, with the card's pitch, volume ramps and
  timing; a 5 Hz high-pass stands for the card's AC-coupled output, and the mix is normalised to 90 % of its peak.
- **Pictures.** The hand-drawn pictures are shown as faithful high-resolution redraws by default (see below);
  `--original-art` shows the original pixels as sharp squares with anti-aliased edges. Palette fades are colour
  multiplies, so they apply to both.
- **Sharper.** Effects are evaluated per output pixel; maps are read filtered and their colours looked up in true
  colour; 6-bit palette products, 1/256 weights and 64-level shade ramps are continuous; p2's blur, p4's fire and
  p6's shadow masks run in true colour or as continuous fields.
- **3D engine** (`src/dope/engine.h`). Vertices are transformed and projected in float with the original's matrix
  and projection (the 0.75 vertical scale and each object's projection factor kept), rasterised with 4× MSAA, with
  the env and texture coordinates interpolated perspective-correctly. The env maps are read filtered through the
  part's palette. Each bump texel keeps one offset, as the fillers read it, with the env read inside it filtered and
  an edge about one pixel wide between texels (the patchy relief of the original; reading the offsets filtered
  between texels turns it into smooth veins, `dope/re/dump/*-bump-compare.jpg`, not in Git). A depth buffer orders
  the faces; between p2's discs and "C" it compares the original's sort key, so the "C" shows between the discs as
  in the original. The original's back-face test multiplies in 16 bits and overflows on faces larger than about 181
  pixels: the engine evaluates that test on the integer screen positions the original computes and drops or draws
  those faces as the original does (`EnginePart::cull16`); every other face is culled by its float winding. It shows
  in p2's close-ups, where the near disc's big faces vanish and the "C" and its stand show through.

## The art switch and the redraws

The remake shows faithful high-resolution redraws of the hand-drawn pictures: the eleven labels, the credits,
"dope", the "Complex Media Labs" logo, the five backgrounds (the stand, the nebula, the chequerboard, the swirl, the
horizon) and the three env maps. They are AI redraws made with ChatGPT's image generation from the original pixels
and fitted onto them; [`art/README.md`](art/README.md) says how each was made and how closely it fits. `--original-art`
shows the original pixels instead; a part never knows which it draws.

- The logo is redrawn as a grey **shade map** and coloured per pixel by p1's two palettes as they cross-fade, as
  the original's 64 shade indices are; the credits are a grey coverage mask that the blur's text fade uses.
- One redraw serves both parts where the demo shows a picture twice (the nebula in p3 and p4, the chequerboard in
  p4 and p5, the swirl in p5 and p6); the cross-fades between them are true-colour blends.
- Generated maps keep the original's data: p2's env maps, the bump and fur maps, p3's Phong map (fitted as the
  radial gradient it was painted as), the photograph and spots of p7's light map, p4's cloud and landscape maps.

## Checks

Numbers from the final check (Linux, i7-13700H, music time in seconds):

- **Against the capture**: contact sheets of the remake (both art modes) next to the capture every 8 s over the
  whole demo (`tools/compare.py`). Mean absolute difference to the capture (320×200, 0..255) with the original
  pixels: 0.2–1.9 for the logo and the still pictures, 1–9 for the moving parts, 21 in p2's close-up at 138 s (the
  blur without the table's floor is lighter there); with the redraws 1–14.
- **Timing**: the remake rendered one and two retraces before and after a capture frame, the closest kept: the
  sliding labels of p2 to p7 (a pixel per retrace) and the motion of p1, p3, p5 and p6 match the capture's frames to
  the retrace.
- **p2's culling**: after 136 s the "C" and its stand show through the near disc where the capture shows them (an
  overflow can start a retrace or two apart); `tools/enginetest` against the bit-exact model's frames 7500 and 9701
  shows the same dropped faces on screen.
- **Seeking**: 15 times across all parts rendered in ascending and in descending order give identical frames
  (maximum difference 0); 30 frames played at 60 fps and a jump to the last of them differ by at most 1/255 (p2's
  blur, p4's fire: the history rebuilt after a jump).
- **Judder**: 48 consecutive frames at 60 fps at 14 moments in every part change smoothly from frame to frame (no
  repeated frames; the change per frame varies by at most 6 % of its mean from one frame to the next).
- **Speed**: GPU time per frame at 2560×1920 over each whole part at the steps of 60 fps playback (`--bench T0,T1
  --fps 60`), median / 95th percentile in ms. The first frame of a part allocates its buffers (and p2's rebuilds
  its blur: up to 80 ms). Before the blur was changed to one blend per retrace, p2 took 14–16 / 15–21 ms on the
  Iris Xe.

  | part | Intel Iris Xe (integrated) | GeForce RTX 4060 Laptop (Mesa zink on NVK) |
  |---|---|---|
  | p1 | 2.13 / 2.71 | 0.43 / 0.50 |
  | p2 | 4.35 / 6.86 | 4.02 / 7.23 |
  | p3 | 2.59 / 3.19 | 2.77 / 3.31 |
  | p4 | 2.92 / 6.39 | 1.28 / 2.84 |
  | p5 | 1.99 / 2.70 | 2.55 / 3.19 |
  | p6 | 2.32 / 2.66 | 2.90 / 3.41 |
  | p7 | 2.52 / 3.82 | 2.78 / 3.64 |

- **Start-up**: ready in 420 ms (unpacking `DOPE.EXE` 18 ms, the soundtrack 207 ms, the seven parts 130 ms, the
  rest the pictures and the GL set-up); 490 ms from the start of the process to the first frame written.
- **Music**: `test_music` matches the capture: the 281,388 GUS register and port writes from the GUS init to the
  stop, the 1 MB of GUS memory after the upload, the music position after every timer interrupt in 32,113 frames, and
  the 22,203 tick times (to the capture's interrupt latency).
- **Builds**: g++ 16.2 and MinGW (x86_64-w64-mingw32 16.2) build without warnings (`-Wall`). `--wav` writes the
  461.98 s soundtrack (48 kHz, peak −0.9 dBFS) in 0.55 s; `--video` renders a range with sound through ffmpeg (4 s
  at 1280×960 and 60 fps in 2.3 s).

## Known differences

- **The start-up** (the loader's text-mode message while the module loads) is skipped: the remake starts with the
  music. The end is the moment p7 sets text mode; the lines TERM and CDPI print in text mode afterwards are not shown.
- **Disk time.** On a real PC each part starts later by the time it takes to read and unpack it, and the pictures
  drift later against the music by an amount that depends on the machine; the remake follows the capture, which has
  none.
- **Three fades at hardware speed** (above): from p5's hand-over on, the remake runs later against the music than
  the capture.
- **The 3D engine** (p2, p3, p5, p6, p7) transforms and projects the objects in float and rasterises them with MSAA,
  instead of whole-pixel vertices and integer spans. Where a bump map is smaller than a pixel (thin rings, far or
  bumpy objects) the remake averages it over the pixel; the original shows aliasing noise there (p7's speckled
  tentacle, p6's striped rings, p3's grainy bumps). The 16-bit back-face test is evaluated on the remake's integer
  positions, which are within 2 pixels of the original's: faces thousands of pixels off screen may be decided
  differently, and an overflow can start a retrace earlier or later. Dropped as 8-bit artefacts: the trivial reject
  of triangles with every corner left or right of the screen, the never-drawn column 319 of clipped env-only spans,
  the carries between packed 16-bit halves.
- **p1.** The logo scrolls continuously instead of one pixel every second retrace, and its violet-to-orange
  cross-fade is continuous instead of 1/256 steps every 8 retraces with both products floored (up to 2/63 darker).
- **p2's motion blur** blends once per retrace with the script's weight, as the original does on a PC that draws
  every retrace; in the capture's slow close-ups (46–62 drawn frames per second) the original's trails are shorter
  and fall apart into separate copies. Between two retraces the screen mixes the blends before and after. The blend
  is a moving average of continuous ramp levels without the table's floor, so a still picture under heavy blur is
  not darkened (the original sits up to 256 / s ramp levels lower: 10 at s = 24, 23 at s = 11; the close-ups after
  136 s are visibly lighter than in the capture). The credits fade in to full white instead of stopping a few
  shades short. After a seek the blur is rebuilt from black, starting far enough back that the history before it
  weighs less than 1/1000 (at most 1/255 different from playing through).
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
  darker for p5), and p5 shows it at that brightness too, under the torus and in the hand-over.
- **p5.** The BUMP glow's depth pulses continuously (no truncated table, no floored offsets). Red-map lookups that
  leave the map at the silhouette are clamped to its edge.
- **p6.** The cat's light map is its radial formula with a continuous shade instead of 64 levels. The 32 shadow masks
  (64 x 64 bits, switched every 4 retraces) are smoothed into distance fields and interpolated between neighbouring
  masks (centred on the 4 retraces each original mask is shown), so the ring shadows glide with smooth edges instead
  of jumping in 2-texel steps. The material morph and the fur's strength change continuously.
- **p7.** The light map is rendered at 4 x 4 samples per texel with the five spots at continuous positions (up to 1.8
  texels from the original's whole-texel ones) and the photo and spot interpolated; its level runs continuously
  through the grey ramp. Through the averaged bumps the faint photo shows as a smooth marbling of the tentacle where
  the original shows grainy noise. The bump table's packed-word carry (codes without a column offset read one row
  up) is dropped.
