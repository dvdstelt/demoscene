# Reve by Pulse (The Party '95) — high resolution remake

A C++/OpenGL re-implementation of *Reve*, the PC demo by Pulse (code LCA, graphics Antony and AJT, music Shad) that
came 5th in the PC demo competition at The Party 1995, in its final version of May 1996. It runs on Linux and Windows
at any resolution in true colour, instead of 320×200 in 256 colours, with the original's motion, timing and music,
and with high-resolution redraws of its pictures, title and names (or the original pixels, `--original-art`).

The remake reads everything from the original file, in the original release in [`reve/original/`](../original/).

| what | where | how it is used |
|------|-------|----------------|
| the program | `REVE.EXE`: the Infiny Loader and LCA's FLAT OS extender, then an archive in "LCA's Packer" format with the demo program `REVE.LFL` (a flat 32-bit image with a relocation table) | the archive's LZW unpacked and the program relocated at start-up by `src/unpack.cpp`, checked against the known release |
| the data | the other 35 files of the archive: `GFX\*.RAW` pictures with their palettes, `GFX\*.PAL` tables, `3D\*.WLD` worlds and `3D\*.ANI` animations, `ZIC\PULSE.MOD` | read by the file names the program passes to its loader |
| the music | `ZIC\PULSE.MOD`, an 8-channel MOD by Shad | played by a port of LCA's MOD-Player V3.03 (`src/lcaplay.cpp`) on a model of the GUS GF1 (`src/gf1.cpp`) |
| the running order | the script table at 0x205ed0: per entry an init, a retrace routine, a frame routine and the music position that ends it | `src/timeline.cpp` |
| LCA's 3D engine | the world loader 0x21d440, the animation 0x25f3ae, the render 0x25f5b5, the face sort 0x21e6ec and the blur pass 0x2045fa | ported once (`src/reve/engine*`) and used by the nine 3D entries |
| the rest | file names, constants, speeds, bounds and key lists | read by address in the program image where the parts need them |

All addresses are those of the running demo, with the program image at 0x20427c; the notes in [`../re/`](../re/)
explain every one of them.

## Running

The program looks for `REVE.EXE` next to itself, in its parent folders, in the current folder and its parents, and in
an `original` folder in any of those, so `reve/original/` is found from the build folder. Or pass `--data DIR`. The
redraws are found the same way in an `art` folder (`reve/remake/art`); or pass `--art DIR`.

```
reve-remake [--window] [--size 1280x960] [--start SECONDS] [--ss 2] [--loop] [--info] [--original-art]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↓/↑` seek ±30 s, `F` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples. `--info` shows the time, music position and part in the
title bar. `--original-art` shows the original pixels of the pictures, the title and the names, and the original
texture sheet, instead of the redraws and the upscale.

Offline: `--wav out.wav` writes the soundtrack; `--shots 10,62.5 --shotdir DIR --shotsize WxH` writes the frames
at those music times (seconds) to PNG; `--video out.mp4 [--from T0 --to T1] [--fps 60] [--shotsize WxH]` renders
the whole demo, or a range, with sound through `ffmpeg`; `--bench T0,T1 --shotsize WxH` reports the GPU time per
frame.

For checks: `--refshots R1,R2` writes the frames at refreshes of the original's capture (see below), `--start` takes
`--refresh R` instead of seconds, `--cues` prints the running order, `--picshots all` draws every picture. The
environment variable `REVE_ENGINE=original` makes the 3D engine draw as the original does (its integer arithmetic,
affine mapping, nearest texels, the exact tables and the blur pass on indices) for pixel comparisons at
`--shotsize 320x200`.

## Building

Requirements: a C++20 compiler, CMake ≥ 3.20 and SDL3. If SDL3 is not installed, CMake downloads and builds it;
`-DREVE_FETCH_DEPS=ON` always does that. No other libraries are used.

**Linux (Fedora)**

```
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel
cmake -S . -B build -G Ninja && cmake --build build
./build/reve-remake --window
```

On Fedora Atomic or without root, the same works inside a toolbox.

**Windows from Linux (MinGW cross-compile, Fedora)**

```
sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-winpthreads-static
mingw64-cmake -S . -B build-win -G Ninja && cmake --build build-win
```

This gives `build-win/reve-remake.exe` and `SDL3.dll`. Copy both anywhere, with `REVE.EXE` next to them or in an
`original` folder next to them, and the `art` folder next to them. (The program is called `reve-remake` so it can
never overwrite `REVE.EXE`.)

**Windows (Visual Studio 2022)**: `cmake -S . -B build` then `cmake --build build --config Release` (SDL3 is
built from source). Only the MinGW build has been tried.

## How it is put together

- `src/main.cpp` options, playback loop, shots / video / bench; `src/demo.*` the framework (parts, the clock in
  screen refreshes, the script, the cues, render targets); `src/timeline.cpp` the running order; `src/player.*`
  audio output
- `src/exe.*`, `src/unpack.cpp` reading `REVE.EXE`; `src/pictures.*` the pictures, the redraws and the upscale
- `src/music.*` the soundtrack and the clock; `src/lcaplay.*` LCA's MOD player; `src/gf1.*` the GF1 model. The
  soundtrack is rendered at load time (about 0.1 s)
- `src/reve/` the 3D engine: `engine.*` (the worlds, the animation, the lighting and the sort on the CPU),
  `engine_gl.cpp` (textures, shading, the painter's-order draw, the blur), `original.cpp` (`REVE_ENGINE=original`)
- `src/scenes/` one port per part: `title`, `credits` (the 3D knot and the names), `strobe` (the flashing pictures),
  `wiz` and `dots` (with `roto.h`, the rotozoom layers), `twirl` (PLAYMO and BOUCHE), `p3d1`, `part2`, `p3d3`, `part4`
  and `fin` (the 3D worlds), `dada` and `tomboz` (with `planes.h`, the interlaced planes), `stills` (the still
  pictures and the closing title)
- `art/` the redrawn pictures and how they were made (`art/README.md`); `tools/align_art.py` makes the picture to
  upload, fits a redraw onto the original and writes the comparison sheet; `tools/upscale_art.py` writes the texture
  sheet's upscale
- `tools/compare.py` puts remake frames next to the original's capture; `tools/test_music.cpp` checks the GUS
  register stream, the GUS memory and the music positions against the capture
- [`../re/PORT.md`](../re/PORT.md) the conventions every part follows; `../re/` the decompiled notes (pseudo-C with
  every constant), the emulator harness and the scripts that make the reference capture

## Fidelity

Every routine was decompiled, modelled in Python and checked against the original running in an emulator, then
ported and compared with a capture of the original made in that emulator (see [`../re/NOTES.md`](../re/NOTES.md) to
make it yourself). The capture holds one frame per screen refresh, 17,729 of them; `--refshots` renders the same
refreshes.

- **Timing.** The original runs on the screen refresh (70.086 Hz): its timer interrupt tunes its own period until it
  fires just before every vertical retrace, and there it runs the current entry's retrace routine, which steps the
  part's motion once per refresh. The main loop draws at most one frame per refresh. The remake's clock is the
  refresh, as a fraction: per-refresh state is replayed from the entry's start exactly as the original steps it and
  blended by the fraction of a refresh, so motion is smooth at any frame rate and seeking is exact. Each entry starts
  on the refresh on which the capture shows its first retrace call; the gap before it shows what the capture shows
  there (the last frame held, black, or the entry's first frame).
- **Music.** LCA's MOD-Player V3.03 is ported routine by routine. Its GUS register writes (111,241), the GUS memory
  after the sample upload, the GUS timer interrupts and the music positions match the capture exactly. The voices
  are mixed at the output rate with the card's pitch, volume ramps and timing; the mix goes through the line output's
  high-pass and is normalised to −1 dBFS.
- **Colours.** Every colour is the palette entry of the pixel's index, as a continuous value instead of 6 bits.
  Pictures and textures are filtered in colour. GOURAUD.PAL's 16 shade levels are evaluated at a continuous level,
  blending the colours of the two neighbouring rows. The strobes' negatives are the exact inverse colours.
- **3D.** The engine's transform, culls, lighting and painter's order run on the CPU as the original's do, without
  the integer truncations; the faces are drawn perspective-correct with filtered texels, continuous light levels and
  4× MSAA. The order of the faces stays the original's: a depth buffer would change 0.4 to 4.7 % of the pixels, with
  whole surfaces swapping. The blur pass 0x2045fa that softens the credits and the 3D worlds (one pass through
  `ANTIALIA.PAL`, the average of a pixel and its neighbours above, below and to the left) is the same average in
  colour, each tap one original pixel wide; each index first gets the colour the table gives it in a flat area, which
  is why PART4's letters are white. With `REVE_ENGINE=original` 101 of 120 sampled capture records of the 3D parts
  match to the pixel, and most of the others are records the capture took while a frame was being written.
- **Pictures.** The still pictures, the strobed pictures, WIZ, the twirled pictures, the planes' textures, the title
  and the four names are shown as faithful high-resolution redraws by default, and the texture sheet of the 3D parts
  and the rotozooms gets the detail of a 4× upscale (see below); `--original-art` shows the original pixels, filtered.

Known differences:

- **Init times.** The original's inits unpack files and build tables, which takes 1 to 12 refreshes on the capture's
  emulated machine; the remake takes the capture's times, so the parts start where they start in the capture. On a
  real machine they depend on its speed.
- **Latency.** The capture shows each 3D frame one or two refreshes after the state it draws (the original draws them
  at 45 to 60 frames per second). The remake shows the state at the current time, so the capture lags it there.
- **The strobes** flash for exactly one refresh. On a display slower than 70 Hz a flash can fall between two
  displayed frames (at 60 Hz about one in seven). The negatives are the exact inverse colours, where the original
  takes the palette entry nearest to each inverse; that changes their hue a little, most visibly on BOUGE's head,
  which is grey-blue instead of green.
- **The rotozoom of WIZ** writes each layer to a quarter of the pixels per frame, so the original shows a fine
  flickering checker of the two layers, each pixel class drawn 0 to 3 frames earlier. The remake shows the average of
  the classes at those times. The ghosts of the shrinking sprite (the buffer is never cleared) and the black pixel
  classes of the first three frames are not reproduced.
- **The twirls** turn each point continuously instead of moving whole rings of pixels, so the picture does not tear
  along the rings. The centre pixel, which the original's ring lists give to a ring past the sine table, and
  BOUCHE's two-pixel dot over it are left out.
- **The dot spirals** keep their feedback in colour, which leaves out the colours that stick in the original's
  palette-quantised blend, its dither and the pixel-sized dots. The frames are shown one per refresh, not blended, as
  the spiral's look is the stroboscopic effect of the original's 70 frames per second.
- **The planes** of DADA and TOM / BOZ keep the original's line interlace but give every pixel its own ray and read
  the textures filtered, with an anti-aliased horizon.
- **FIN's trail** blends each frame into the screen as the original does, once per refresh; the remake cuts it
  after eight frames, the oldest taking the remaining weight.
- **Near the camera** faces are clipped at the original's near plane with interpolated texture coordinates and light;
  the original gives the new vertices texture coordinate 0 and full brightness. PART4's box edges are clipped there
  too instead of going to screen positions from earlier frames.
- **Music clock.** The capture's emulated machine held back one music tick by one record; the remake's is on time.
  The output is 48 kHz.

## The redrawn pictures

Reve draws its pictures as they are: the title, the four names in the credits, the strobed and still pictures, WIZ,
the two twirled pictures and the three textures of the planes. They are AI redraws made with ChatGPT's image
generation from the original pixels and fitted onto them ([`art/README.md`](art/README.md) has the prompts, the
attempts and the fit). The title and the names are fitted letter by letter; MRTOMMY's head in `TOM.RAW` is cut from
the MRTOMMY redraw.

`GFX\TEXTURE.RAW`, the sheet of 64 × 64 tiles that the 3D parts map through GOURAUD.PAL's shade levels and the
rotozooms and DADA's planes wrap, is not drawn as it is. It has a 4× upscale instead (`art/tex_texture.jpg`, made with
Real-ESRGAN by `tools/upscale_art.py`), which keeps every pixel where it is. The 3D parts still compute each colour
from the original's indices and tables and add the upscale's detail, scaled by how much of a change in the texel's
colour the shade level passes on (`src/reve/engine_gl.cpp`); the 2D parts read the upscaled tiles directly. The tile
of noise blocks keeps the original pixels.
