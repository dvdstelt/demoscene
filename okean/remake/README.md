# The Sea Robot of Love by Orange (The Party '95) — high resolution remake

A C++/OpenGL re-implementation of *The Sea Robot of Love*, the 64k intro by Orange (code hoplite and dune, music
dune, font and graphics der piipo) that came 3rd in the PC intro competition at The Party 1995. It runs on Linux and
Windows at any resolution in true colour, instead of 320×200 in 256 colours, with the original's motion, timing and
music, and with a high-resolution redraw of der piipo's font (or the original pixels, `--original-art`).

The remake reads everything from the original program. It is not included: get it with
`scripts/fetch-originals.sh okean` (or `.ps1` on Windows) from the repository root, which puts it in
`okean/original/`.

| what | where | how it is used |
|------|-------|----------------|
| the program image | `KORSO2.EXE`: Tran's PMODE and one 32-bit program, packed with Rob Northen's ProPack | unpacked at start-up by `src/rnc.cpp` (the packer's method 1) and checked against the known release |
| the music | the 8-channel ProTracker module inside the program (code32 0xae0) | played by a port of the intro's own player and GUS driver (`src/modplay.cpp`) on a model of the GUS GF1 (`src/gf1.cpp`) |
| the clock | the parts' retrace timers and exit tests, the player's GUS timer | `src/schedule.cpp`: when every music tick is served and when every part starts and ends |
| the parts' data | palettes, the sine table, meshes, maps, blob kernels (the immediates of the line drawers' `add` instructions), the title's mask, the font, the texts | read by address in the program image (code32 offsets, as in the notes) |

All addresses are code32 offsets of the unpacked program (file offset 0xec0 + address); the notes in
[`../re/`](../re/) explain every one of them.

## Running

The program looks for `KORSO2.EXE` next to itself, in its parent folders, in the current folder and its parents, and
in an `original` folder in any of those, so `okean/original/` is found from the build folder. Or pass `--data DIR`.
The redraws are found the same way in an `art` folder (`okean/remake/art`); or pass `--art DIR`.

```
okean-remake [--window] [--size 1280x960] [--start SECONDS] [--ss 2] [--loop] [--info] [--original-art]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↓/↑` seek ±30 s, `F`/`F11` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples. `--info` shows the time, music position and part in the
title bar. `--original-art` shows the original pixels of the font instead of the redraw.

Offline: `--wav out.wav` writes the soundtrack; `--shots 10,62.5 --shotdir DIR --shotsize WxH` writes the frames
at those times (seconds) to PNG; `--video out.mp4 --from T0 --to T1 [--fps 60] [--shotsize WxH]` renders a range
with sound through `ffmpeg` (by default the whole intro); `--bench T0,T1 --shotsize WxH` reports the GPU time per
frame.

For checks: `--refshots R1,R2` writes the frames shown at records of the original's capture (see below), `--start`
takes `--refresh R` instead of seconds, `--cues` prints the running order, `--picshots all` draws every picture.

## Building

Requirements: a C++20 compiler, CMake ≥ 3.20 and SDL3. If SDL3 is not installed, CMake downloads and builds
it (3.2.24); `-DOKEAN_FETCH_DEPS=ON` always does that. No other libraries are used.

**Linux (Fedora)**

```
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel
cmake -S . -B build -G Ninja && cmake --build build
./build/okean-remake --window
```

On Fedora Atomic or without root, the same works inside a toolbox.

**Windows from Linux (MinGW cross-compile, Fedora)**

```
sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-winpthreads-static
mingw64-cmake -S . -B build-win -G Ninja && cmake --build build-win
```

This gives `build-win/okean-remake.exe` and `SDL3.dll`. Copy both anywhere, with `KORSO2.EXE` next to them or in an
`original` folder next to them, and the `art` folder next to them.

**Windows (Visual Studio 2022)**: `cmake -S . -B build` then `cmake --build build --config Release` (SDL3 is
built from source). Only the MinGW build has been tried.

`-DOKEAN_PARTS="title;objects"` builds only some parts (the others show a placeholder); `cmake --build build --target
test_music` builds the music checker.

## How it is put together

- `src/main.cpp` options, playback loop, shots / video / bench; `src/demo.*` the framework (parts, the clock in
  screen refreshes, render targets); `src/schedule.*` the clocks (music ticks, part changes, the end); `src/timeline.cpp`
  the running order; `src/player.*` audio output
- `src/exe.*`, `src/rnc.cpp` reading `KORSO2.EXE`; `src/pictures.*` the pictures and the redraws
- `src/music.*` the soundtrack; `src/modplay.*` the intro's player and GUS driver; `src/gf1.*` the card and the GF1
  model. The soundtrack is rendered at load time (under 0.1 s)
- `src/okean/` the core library: `glow.*` (the sine table, palettes, blob kernels and the canvas: noise plus blob
  lines, resolved through the palette), `text.*` (the shared caption printer and the font)
- `src/scenes/` one port per part: `title`, `objects`, `web` (the green canyon), `morph`, `landscape`, `star`,
  `sheet`, `end`; `blobline.h` and `landlines.h` the line drawers' layouts some of them share
- `art/` the redrawn pictures and how they were made (`art/README.md`); `tools/align_art.py` makes the pictures to
  upload, fits a redraw onto the original and writes the comparison sheet
- `tools/compare.py` puts remake frames next to the original's capture; `tools/test_music.cpp` checks the GUS
  register stream, the GUS memory, the music positions and the schedule against the capture
- [`../re/PORT.md`](../re/PORT.md) the conventions every part follows; `../re/` the decompiled notes (pseudo-C with
  every constant), the emulator harness and the scripts that make the reference capture

## Fidelity

Every routine was decompiled, modelled in Python and checked against the original running in an emulator (the
models reproduce every captured frame of every part byte for byte), then ported and compared with that capture (see
[`../re/README.md`](../re/README.md) to make it yourself). The capture holds one record per screen refresh, 9,109 of
them; `--refshots` renders the same records.

- **The 50 Hz mode.** The intro sets its own screen mode, 49.85 refreshes per second, and runs every part's motion
  on it. The remake's clock is that refresh, as a fraction: state the original steps once per refresh is evaluated
  in closed form or replayed from the part's start, and blended within a refresh, so motion is smooth at any frame
  rate and seeking is exact. (The original's `-M`, which keeps 70 Hz and so runs the motion 40 % faster, is not
  offered.)
- **Timing.** The parts start and end in the same refreshes as in the capture: `src/schedule.cpp` runs the parts'
  exit tests (a music order, or a fade reaching zero) on the music's own clock, GUS timer 2 at 19.84 ms per tick,
  including the tick each part's timer calibration holds off and the one it loses. Record k shows the frame drawn
  after k − F callbacks of the part (F its first callback); captions change in the same records.
- **Music.** The intro's player and GUS driver are ported routine by routine. Their GUS register writes (70,027
  lines), the GUS memory and the music positions match the capture exactly (`test_music`). What the card plays is
  kept: the song is about a third of a semitone sharp (the player's divisor 37 against the card's 37.68 Hz per
  step at 16 voices), volumes run 0..63, the pans are the loader's fixed ones, every note starts with the old one's
  volume ramp, and the end part lowers the master volume with the picture. The voices are mixed at the output rate
  with the card's pitch, ramps and timing; the mix goes through the line output's high-pass and is normalised to
  −1 dBFS.
- **Drawing.** Every part draws as the original does: per-pixel noise of the part's levels, then lines as rows of
  8×8 brightness "blobs" added to it every 2 or 4 pixels, the sum looked up in the part's palette ramp. The remake
  accumulates the same sums in a float buffer at the output resolution: each blob is the original's kernel (read
  from the line drawer's code) as a smooth surface over its pixels, at its exact position instead of the 320×200
  grid, and the sum is looked up with linear interpolation between palette entries. Projections, steps and the sine
  table are evaluated in floating point.
- **The title.** The beams are smooth bands of the stamps' average light. The letters are what they are in the
  original: the shadows left where stamps whose corner lies on the 1-bit lettering are skipped, the mask averaged
  along the stamps that cover each pixel, so they are soft and offset by the stamp's half width, at any resolution.
- **The landscape.** The 256×256 height map is generated at start-up exactly as the original does (it equals the
  original's byte for byte). Lines behind nearer ridges are hidden by the original's floating horizon, evaluated
  per drawn point at eight columns per original pixel, keeping which lines show. (The caption says "moving
  lightsource and shadow"; the code has no light: the dark areas are the hidden lines.)
- **Text.** The captions come from the original's shared printer, a page per 16 rows of the music, in the records
  in which the original shows them; the end texts with their own layout. The font's outline takes each part's own
  dark colour and fades with it.
- **Noise.** Every part fills its background with fresh random noise of its levels (8, 16 or 32 dark palette
  entries, grey in the end part) every refresh. The remake keeps the levels and the per-refresh change but draws it
  at a third of an original pixel (frame height / 600), so it reads as fine TV snow rather than 320×200 squares.

## Known differences

- **Dropped frames.** The capture's emulated PC missed a retrace now and then (151 records repeat the previous one):
  a music tick with new notes just before the retrace keeps the CPU in the GUS interrupt, whose handler waits there
  for the card's volume ramps. The remake draws every refresh. One of those drops moved the start of the sheet part
  by a refresh in the capture (6896 instead of 6895); in the remake it starts a refresh earlier, and so do two music
  ticks around it (by up to 22 ms).
- **Frame-rate dependent motion.** The canyon banks on the heading of 32 drawn frames earlier; the remake uses 32
  refreshes, as a machine that draws every refresh does (the capture's own dropped frames change three of its
  records). The noise changes once per drawn frame in the original, once per refresh in the remake.
- **8-bit artefacts left out.** Sums past 255 (which wrap to dark and carry into the next pixel), the grey of
  index 254 reached by a sum, blobs clipped whole at x 311 / y 191 (lines end 1..8 pixels before the right and
  bottom edges in the original, about 4 before the left and top), the line drawers' 8-bit step counters (long
  off-screen lines that stop mid-screen for a frame), the title's full-height vertical bar in 15 frames (a clipping
  overflow) and its nearly horizontal beams that lose their outer part to an unsigned sort.
- **Fades** are continuous. The original truncates `palette × fade` to whole 6-bit steps, which makes its fades a
  little darker and its last steps black where the remake still shows a faint picture.
- **Vertices behind the camera** keep the original's projection, which throws them far out: the spokes of the star
  part's web and of the objects part are part of the picture. Edges with both ends behind the camera, which never
  reach the screen in the original, are not drawn.
- **The set-up records** at each part change (two or three refreshes) show black or the previous part's last frame
  as in the capture, without the one-refresh flashes of the next part's palette on the previous frame.

## The redrawn pictures

The intro draws one hand-made picture as it is: der piipo's font, 26 lowercase letters 38 pixels high with a dark
outline, used for the captions and the end texts. The remake shows a faithful AI redraw of it, made with ChatGPT
from the original and fitted letter by letter; [`art/README.md`](art/README.md) says how. The title's lettering is
a 1-bit mask that the light beams skip; it is only ever seen blurred by the 8-pixel stamps, so it is used as it is.
