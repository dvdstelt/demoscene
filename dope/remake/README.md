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
`-DDOPE_SILENT_MUSIC=ON` builds a silent stand-in for the soundtrack (`src/stub`, also used while `src/music.*` do
not exist); `-DDOPE_EXTRA_SOURCES=FILE` adds a source file from anywhere (experiments).

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
