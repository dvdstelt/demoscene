# Inside by CNCD (The Gathering '96) — high resolution remake

A C++/OpenGL re-implementation of *Inside*, the PC demo by CNCD (code Wili, Jouni and Juliet, graphics Destop,
music Yolk and Legend, additional graphics Santtu) that won the PC demo competition at The Gathering 1996. It runs on
Linux and Windows at any resolution in true colour, instead of 320×200 in 256 colours, with the original's motion,
timing and music, and with high-resolution redraws of its title, the names in the credits and the photos of people
(or the original pixels, `--original-art`).

The remake reads everything from the original files. They are not included: get them with
`scripts/fetch-originals.sh inside` (or `.ps1` on Windows) from the repository root, which puts them in
`inside/original/`.

| what | where | how it is used |
|------|-------|----------------|
| the program image | `INSIDE.EXE`: PMODE/W 1.22 with the Watcom C program in PMODE/W's compressed "PMW1" format | unpacked at start-up by `src/unpack.cpp` (PMODE/W's decompressor and fixups) and checked against the known release |
| the data | `INSIDE.DAT`: one LZSS-compressed block with a directory of 93 files | unpacked by `src/unpack.cpp`; the parts read the pictures, palettes, shade and blend tables, 3D objects and the LightWave scene by the file names in the program |
| the music | `EYE.AMF` (DSMI's module format) and `SDIGUS.DSD` (DSMI's GUS driver, for its volume table) | played by ports of DSMI's AMP 2.00 player (`src/amp.cpp`) and GUS driver (`src/gusdrv.cpp`) on a model of the GUS GF1 (`src/gf1.cpp`) |
| the running order | the script table at 0x4222e4 | `src/timeline.cpp`: each entry's routine runs when the music reaches its position |
| SurRender, Hybrid's 3D engine | the transform, culling, lighting, sorting and clipping 0x40c6ac and six rasterizers | ported once (`src/inside/engine*`) and used by most parts |
| the rest | file names, constants, positions, rotations and seeds | read by address in the program image where the parts need them |

All addresses are in the unpacked program image (code at 0x400000, data at 0x420000); the notes in
[`../re/`](../re/) explain every one of them.

## Running

The program looks for `INSIDE.EXE`, `INSIDE.DAT`, `EYE.AMF` and `SDIGUS.DSD` next to itself, in its parent folders,
in the current folder and its parents, and in an `original` folder in any of those, so `inside/original/` is found
from the build folder. Or pass `--data DIR`. The redraws are found the same way in an `art` folder
(`inside/remake/art`); or pass `--art DIR`.

```
inside-remake [--window] [--size 1280x960] [--start SECONDS] [--ss 2] [--loop] [--info] [--original-art]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↓/↑` seek ±30 s, `F`/`F11` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples. `--info` shows the time, music position and part in the
title bar. `--original-art` shows the original pixels of the title, the names and the photos instead of the redraws.

Offline: `--wav out.wav` writes the soundtrack; `--shots 10,62.5 --shotdir DIR --shotsize WxH` writes the frames
at those music times (seconds) to PNG; `--video out.mp4 --from T0 --to T1 [--fps 60] [--shotsize WxH]` renders a
range with sound through `ffmpeg`; `--bench T0,T1 --shotsize WxH` reports the GPU time per frame.

For checks: `--refshots R1,R2` writes the frames at refreshes of the original's capture (see below), `--start` takes
`--refresh R` instead of seconds, `--nofade` leaves out the palette fades and flashes, `--cues` prints the running
order, `--picshots all` draws every picture. The environment variable `INSIDE_ENGINE=original` makes the 3D engine
draw as the original does (affine mapping, nearest texels, the exact tables, no anti-aliasing) for pixel comparisons
at `--shotsize 320x200`.

## Building

Requirements: a C++20 compiler, CMake ≥ 3.20 and SDL3. If SDL3 is not installed, CMake downloads and builds
it (3.2.24); `-DINSIDE_FETCH_DEPS=ON` always does that. No other libraries are used.

**Linux (Fedora)**

```
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel
cmake -S . -B build -G Ninja && cmake --build build
./build/inside-remake --window
```

On Fedora Atomic or without root, the same works inside a toolbox.

**Windows from Linux (MinGW cross-compile, Fedora)**

```
sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-winpthreads-static
mingw64-cmake -S . -B build-win -G Ninja && cmake --build build-win
```

This gives `build-win/inside-remake.exe` and `SDL3.dll`. Copy both anywhere, with the original files next to them or
in an `original` folder next to them, and the `art` folder next to them. (The program is called `inside-remake` so
it can never overwrite `INSIDE.EXE`.)

**Windows (Visual Studio 2022)**: `cmake -S . -B build` then `cmake --build build --config Release` (SDL3 is
built from source). Only the MinGW build has been tried.

## How it is put together

- `src/main.cpp` options, playback loop, shots / video / bench; `src/demo.*` the framework (parts, the clock in
  screen refreshes, the script, render targets); `src/timeline.cpp` the running order; `src/player.*` audio output
- `src/exe.*`, `src/unpack.cpp` reading `INSIDE.EXE` and `INSIDE.DAT`; `src/pictures.*` the pictures and the redraws
- `src/music.*` the soundtrack and the clock; `src/amp.*` DSMI's player; `src/gusdrv.*` DSMI's GUS driver;
  `src/gf1.*` the GF1 model. The soundtrack is rendered at load time (under 0.1 s)
- `src/inside/` the core library: `fade.*` (the palette fades and flashes as the script runs them), `indexed.*`
  (8-bit pictures, palettes and shade tables on the GPU), `engine*` (SurRender: objects, materials, the CPU
  pipeline and the GPU rasterizers), `code.*` (reading values the parts' routines store, from their instructions)
- `src/scenes/` one port per part: `intro`, `logo`, `credits`, `sprites`, `flower` (with `lws.h`, the LightWave scene
  reader), `flight`, `water`, `twirl`, `temple`
- `art/` the redrawn pictures and how they were made (`art/README.md`); `tools/align_art.py` makes the picture to
  upload, fits a redraw onto the original and writes the comparison sheet
- `tools/compare.py` puts remake frames next to the original's capture; `tools/test_music.cpp` checks the GUS
  register stream, the GUS memory and the music positions against the capture; `tools/engine_check.py` and
  `tools/blend_fit.py` check the engine and its fitted blends; `tools/intro_frames.py` measures the intro's frame
  steps in the capture
- [`../re/PORT.md`](../re/PORT.md) the conventions every part follows; `../re/` the decompiled notes (pseudo-C with
  every constant), the emulator harness and the scripts that make the reference capture

## Fidelity

Every routine was decompiled, modelled in Python and checked against the original running in an emulator, then
ported and compared with a capture of the original made in that emulator (see [`../re/README.md`](../re/README.md)
to make it yourself). The capture holds one frame per screen refresh, 13,081 of them; `--refshots` renders the same
refreshes.

- **Timing.** The original counts everything in screen refreshes: its timer interrupt runs four times per refresh
  (70.086 Hz), the music player runs in it, and the parts' tick routines step once per refresh. The remake's clock is
  the refresh, as a fraction: per-tick state is replayed from the part's start exactly as the original steps it and
  blended by the fraction of a refresh, so motion is smooth at any frame rate and seeking is exact. The script's
  entries run when the music reaches their row, in the interrupt in which the row starts.
- **Music.** DSMI's player and GUS driver are ported routine by routine. Their GUS register writes (140,095), the
  GUS memory after the sample upload and the music positions match the capture exactly. The voices are mixed at the
  output rate with the card's pitch, volume ramps and timing; the mix goes through the line output's high-pass and
  is normalised to −1 dBFS.
- **Colours.** The palette fades and flashes are continuous instead of 6-bit steps. Pictures and textures are
  8-bit indices on the GPU, looked up in the palette, and filtered in colour. Shade tables (a palette index per light
  level and texel) are evaluated at a continuous level, blending the colours of the two neighbouring rows. Tables
  that blend with what is already on the screen cannot be looked up in a shader; each row is fitted as
  `out = s + b × background` per colour channel over the palette (the fit's error is 0.3 to 3.9 levels of 63,
  `tools/blend_fit.py`) and drawn with dual-source blending.
- **3D.** SurRender's transform, back-face test, lighting, painter's order and clipping run on the CPU as the
  original's do, without the integer truncations; the triangles are drawn perspective-correct with filtered texels,
  continuous light levels and 4× MSAA. With `INSIDE_ENGINE=original` the logo part matches 99.97 % of the original's
  pixels.
- **Randomness.** Watcom's `rand()` is replayed from the original's seeds, so the flight path of the credits, the
  flares of the temple and the light levels of the flower tube are the original's.
- **Pictures.** The title, the five names in the credits and the four photos of people in the sprites parts are
  shown as faithful high-resolution redraws by default (see below); `--original-art` shows the original pixels,
  filtered. The faces in the credits keep their original pixels.

Known differences:

- **Part starts.** The original runs a script entry at the end of the first frame it draws after the entry's row
  started, 0 to 8 timer interrupts late depending on the machine. The remake runs it on the row, as the original
  would on an infinitely fast machine, so parts and events start up to one refresh earlier than in the capture.
- **Latency.** The original shows each frame one or two refreshes after it read the clock (it draws some parts at
  about 48 frames per second). The remake shows the state at the current time, so the capture lags it by one or two
  refreshes.
- **Music clock.** The emulated machine of the capture lost one timer interrupt during the credits (at 43.4 s); the
  remake loses none, so from there its music runs 3.4 ms ahead of the capture's. The output is 48 kHz.
- **The intro's light** is a simulation the original steps once per drawn frame (0.69 frames per refresh in the
  capture). The remake replays it at the capture's frame steps and blends between them, and samples the light field
  with a smooth filter instead of one level per original pixel.
- **The credits.** The tunnel's seam is one texture width (u 4096) where the original has 4101, so the texture does
  not jump there. The water mask and the names are drawn through the fits of their blend tables (in the part itself,
  as the tables darken by subtraction), which tint the mask's darkest corners slightly blue where the original is dark
  red. A sixth name picture in the data, SANTTU (`kredu6.raw`), is never shown by the original and is not shown here
  either.
- **The photo parts.** The room is mapped perspective-correct, so its wall textures are not bent as the original's
  affine mapping bends them; the warp has no kinks at its 8 × 8 grid cells. The original's first frame after each
  picture change has one tick less; it is under a white flash and not reproduced.
- **The flower tube.** A tube vertex with light 1023 reads one row past the end of `jeesus.shd`, so the original
  draws a few pixels from the bytes after it (a red sliver at refresh 6000); the remake clamps the row.
- **The flight.** The landscape has a depth buffer and fog per pixel instead of the painter's order and fog per
  vertex. The chase camera's motion depends on the part's tick counter, which runs two interrupts behind the
  capture's (part starts, above), so the paths drift apart by up to about 40 units and the camera's jump away from a
  ship happens at refresh 7861 instead of 7863. The random generator's state at the flight's start is the capture's;
  on a real machine it depends on how many frames the earlier parts drew. The lens flares add half their colour,
  which is what `flare.shd` tabulates (mean error 1.4 of 63).
- **Under water.** The original steps the fish, the drops and the ripple once per drawn frame, so on a real machine
  they depend on its frame rate. The remake replays the capture's 1,172 frames (their clock values and random seed,
  `src/scenes/water_frames.h`) and blends between them at an even frame clock. The ripple's height is sampled
  smoothly, which leaves out the checkerboard waves at the grid's own frequency; the stuck rows along the edges are
  simulated but not shown. The glass fish blend through the fit of `water.int` (1.5 of 63).
- **The twirl** is evaluated per pixel, so the original's staircase rings at strong twists are gone.
- **The temple.** The inside of the pillars and the cracks between faces are black, where the original shows
  leftovers of older frames; the hall's texture streaks are smooth curves instead of zigzags; a few rows of
  `flare.shd` blend pinker over blue than a linear fit can, so overlapping halos are slightly paler.
- **Faces near the camera** are clipped at the original's near plane instead of being dropped.

## The redrawn pictures

Inside draws two kinds of hand-drawn picture as they are: the title "INSIDE" in the logo part (`logo/inside.raw`)
and the five name pictures of the credits (`credits/kredu*.raw`). It also draws four photos of people as they are,
in the sprites parts (`nigga/sprite*.raw`). They are AI redraws made with ChatGPT's image generation from the
original pixels and fitted onto them ([`art/README.md`](art/README.md) has the prompts, the attempts and the fit):

- `art/logo_inside.png` is the title, drawn where the original draws it, with the logo part's dark mask
  (`logo/inmask.raw`) still the original's.
- `art/credits_*.png` are the names. Each is a level map that the credits add to the tunnel through a shade table: a
  flat star, the name, and a scanned face of the group member. The redraws hold the star and the letters; the remake
  draws the original's face pixels on top of them.
- `art/sprite*.png` are the photos, redrawn as photos at my request, with transparent backgrounds, drawn unscaled
  where the original draws them. Their faces, hair and prints are the generator's reading of a few pixels each.

The water background was tried as well and keeps its original pixels, and so does the intro's photo collage for
now (`art/README.md` says why). The textures stay
the original's: they are read through shade, fog and blend tables or warped by the effects.
