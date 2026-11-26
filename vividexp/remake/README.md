# Vivid Experiment by Doomsday (Assembly '96) — high resolution remake

A C++/OpenGL re-implementation of *Vivid Experiment*, the PC demo by Doomsday (code Doom and MRI, graphics Dice and
Damac, music Wode and Swallow) that placed 3rd in the PC demo competition at Assembly 1996. It runs on Linux and
Windows at any resolution in true colour, instead of 320×200 in 256 colours, with the original's motion, timing and
music, and with high-resolution redraws of its pictures (or the original pixels, `--original-art`).

The remake reads everything from the original files. They are not included: get them with
`scripts/fetch-originals.sh vividexp` (or `.ps1` on Windows) from the repository root, which puts them in
`vividexp/original/`.

| what | where | how it is used |
|------|-------|----------------|
| the program image | `VIVID.EXE`: PMODE/W 1.22 with the Watcom C program in PMODE/W's compressed "PMW1" format | unpacked at start-up by `src/unpack.cpp` (PMODE/W's decompressor and fixups) and checked against the known release |
| the data | three file archives linked into the program (159 files: scripts, pictures, palettes, tables, objects, splines) | read by name (`src/exe.cpp`), the last archive that has a name winning, as the program looks them up |
| the running order | the two scripts `script/demo1.scr` and `script/demo2.scr` | `src/vivid/script.*` reads them as the program does; `src/vivid/timeline.*` runs every line when the music reaches its position, and keeps the palette the screen shows |
| the music | `EXPRMNT1.XM` (part I) and `EXPRMNT2.XM` (part II) | played by ports of MIDAS Sound System 0.5a's XM player (`src/xmplay.cpp`) and GUS device (`src/gusdev.cpp`) on a model of the GUS GF1 (`src/gf1.cpp`), timed by MIDAS's screen-synchronised timer (`src/music.cpp`) |
| the rest | constants, file names, command names | read by address in the program image where the scenes need them |

All addresses are in the unpacked program image (code and archives at 0x400000, data at 0x820000); the notes in
[`../re/`](../re/) explain every one of them.

## Running

The program looks for `VIVID.EXE`, `EXPRMNT1.XM` and `EXPRMNT2.XM` next to itself, in its parent folders, in the
current folder and its parents, and in an `original` folder in any of those, so `vividexp/original/` is found from the
build folder. Or pass `--data DIR`. The redraws are found the same way in an `art` folder (`vividexp/remake/art`); or
pass `--art DIR`.

```
vividexp-remake [--window] [--size 1280x960] [--start SECONDS] [--ss 2] [--loop] [--info] [--original-art]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↓/↑` seek ±30 s, `F`/`F11` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples. `--info` shows the time, music position and scene in the
title bar. `--original-art` shows the original pixels instead of the redraws. `--censor` covers the nude parts of the title picture and of the slides' pin-ups with a mosaic, for a video that is to be published (`scripts/render-videos.sh` uses it): on the redraws those of the title picture and of the second pin-up (the first one's redraw covers her chest), on the original pixels those of the title picture and of the first two pin-ups. Without it, nothing is covered.

Offline: `--wav out.wav` writes the soundtrack; `--shots 10,62.5 --shotdir DIR --shotsize WxH` writes the frames at
those music times (seconds) to PNG; `--video out.mp4 [--from T0 --to T1] [--fps 60] [--shotsize WxH]` renders the
whole demo (or a range) with sound through `ffmpeg`; `--bench T0,T1 --shotsize WxH` reports the GPU time per frame.

For checks: `--refshots R1,R2` writes the frames at refreshes of the original's capture (see below), `--start` takes
`--refresh R` instead of seconds, `--cues` prints the scenes and every script line with the refreshes it runs at.

## Building

Requirements: a C++20 compiler, CMake ≥ 3.20 and SDL3. If SDL3 is not installed, CMake downloads and builds it
(3.2.24); `-DVIVID_FETCH_DEPS=ON` always does that. No other libraries are used.

**Linux (Fedora)**

```
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel
cmake -S . -B build -G Ninja && cmake --build build
./build/vividexp-remake --window
```

On Fedora Atomic or without root, the same works inside a toolbox.

**Windows from Linux (MinGW cross-compile, Fedora)**

```
sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-winpthreads-static
mingw64-cmake -S . -B build-win -G Ninja && cmake --build build-win
```

This gives `build-win/vividexp-remake.exe` and `SDL3.dll`. Copy both anywhere, with the original files next to them
or in an `original` folder next to them, and the `art` folder next to them.

**Windows (Visual Studio 2022)**: `cmake -S . -B build` then `cmake --build build --config Release` (SDL3 is built
from source). Only the MinGW build has been tried.

## How it is put together

- `src/main.cpp` options, playback loop, shots / video / bench; `src/demo.*` the framework (scenes, the clock in
  screen refreshes, render targets, the palette texture); `src/player.*` audio output
- `src/exe.*`, `src/unpack.cpp` reading `VIVID.EXE` and its archives; `src/pictures.*` the pictures and the redraws
- `src/music.*` the soundtrack, MIDAS's timer and the songs' row times; `src/xmplay.*` the XM loader and player;
  `src/gusdev.*` the GUS sound device; `src/gf1.*` the GF1 model. The soundtrack is rendered at load time (about
  0.15 s on a many-core CPU, 0.6 s on one core)
- `src/vivid/` the core library: `script.*` (the scripts), `timeline.*` (when every line runs, which scene draws,
  the palette with its fades), `images.*` (PCX and RAW pictures, palettes, indexed textures on the GPU)
- `src/scenes/` one port per kind of effect: `pictures` and `zoom` (everything the script copies into VGA memory: the
  sky, the texts, the labels, the title, the slides, the winged man, the greetings, and the zooming names of the
  credits; `pictures_vga.h`), `bitmap` (the table-driven bitmap effects), `bump` (the bump mapping), `water` (the
  hearts; `water_engine.h`: the ini, `.m3d` and `.spl` readers and the spline), `generic` (the 3D landscape and blobs;
  `generic_files.h`)
- `art/` the redrawn pictures and how they were made (`art/README.md`); `tools/align_art.py` makes the picture to
  upload, fits a redraw onto the original and writes the comparison sheet
- `tools/compare.py` puts remake frames next to the original's capture; `tools/test_music.cpp` checks the GUS register
  stream, the GUS memory and the music positions against the capture
- [`../re/PORT.md`](../re/PORT.md) the conventions every scene follows; `../re/` the decompiled notes (pseudo-C with
  every constant), the emulator harness and the scripts that make the reference capture

## Fidelity

Every routine was decompiled, modelled in Python and checked against the original running in an emulator, then
ported and compared with a capture of the original made in that emulator (see [`../re/README.md`](../re/README.md)
to make it yourself). The capture holds one frame per screen refresh, 27,690 of them; `--refshots` renders the same
refreshes.

- **Timing.** The original counts everything in screen refreshes: MIDAS's timer interrupt comes just before each
  retrace and adds one to the demo's frame counter, and every effect animates from that counter. The remake's clock
  is the refresh, as a fraction. The scripts' lines run when the music reaches their positions, read at each retrace
  as the original reads them: all 43 of the scripts' waits end at the capture's refresh, and the two parts end at
  refreshes 14,447 and 27,690, as in the capture. The music player's ticks come from a port of MIDAS's timer, which
  schedules the player's ticks and the refresh interrupts on one timer chip.
- **Music.** MIDAS's XM player and GUS device are ported routine by routine. Their 372,039 GUS register writes over
  the whole demo, the GUS memory and the music positions match the capture exactly (`test_music`). The voices are
  mixed at 48 kHz with the card's pitch, volume ramps and timing; the mix goes through the line output's high-pass and
  is normalised to −1 dBFS. The demo plays in mono, and so does the remake.
- **Colours.** The palette the screen shows is kept by the timeline as the original's DAC: the scripts' palettes,
  the pictures' own, the effects' and the fades, which are continuous instead of 16.16 steps that end in 6-bit
  values. Pictures, textures and maps are 8-bit indices on the GPU, looked up in that palette and filtered in colour.
- **Pictures.** The texts over the sky are the original's index arithmetic (the text's level added to the sky's
  index, shown through two palettes) evaluated as a blend in colour, so they work with the redrawn lettering. The
  zooming names are drawn at the continuous scale of the original's formula, their edges from a distance field of
  the redrawn letters. The slides' captions, black script on white that is not redrawn (it quotes a song's lyrics),
  are drawn from their own pixels as ink: their lightness, upsampled and cut at a threshold, gives smooth letters at
  any resolution. The other pictures are drawn as they are, from the redraw or the original pixels; under a fade a redraw takes the fade's colours where the palette is no longer the picture's own. The bitmap effects' photos are their redraws (RGBA) over the effect, with the palette's fade fitted per refresh as a gain and offset of their colours.
- **Bitmap effects.** Eighteen of the nineteen per-pixel tables are the formulas they were made from (distances,
  angles, wobbles, mirrored and tiled copies), evaluated per output pixel; the textures are filtered in colour. The
  offsets carry over from run to run as in the original.
- **Bump mapping.** The heights and slopes are interpolated per output pixel, the waves are their closed formulas,
  and the coastline between water and land is anti-aliased.
- **3D.** Perspective-correct, multisampled triangles; normals, environment and bump coordinates interpolated per
  pixel; the Phong shade continuous between the light table's rows. The original's rules that decide what is seen
  stay: faces with a vertex nearer than the near limit are dropped whole (whole triangles of the landscape's hills
  blink away in its low pass), the terrain's bands from wrapped mapping coordinates stay, and objects are drawn
  farthest first.

Speed: the whole demo at 2560 × 1920 takes a median of 3.2 ms per frame on an integrated Intel Iris Xe (95 % of the
frames under 5.3 ms); start-up, with the unpacking, the soundtrack and every scene's tables, about 0.5 s.

### Known differences

- **Redrawn pictures.** The redraws add detail the 320 × 200 pictures do not have: faces, hair, skin, the star cluster's faint stars, the sunset's clouds. Three of the slides' pin-ups are versions the maintainer chose that differ from the original (`art/README.md`): in the first the catsuit covers her chest, and her pose, face and drawing style differ; the second's face shows under the mask and her latex covers her chest; the fourth's face, hair and lighting differ.

- **No load times.** The remake runs every script command in no time, as an infinitely fast PC would. In the capture
  the 3D scenes' initialisation took 5 and 14 refreshes, and a few pictures' decoding a refresh, so those scenes
  and pictures start up to 14 refreshes earlier than in the capture; the music and every wait are unaffected.
- **No frame lag.** The original draws the water and the 3D scenes at 23 to 70 frames per second on the capture's
  emulated PC and shows each frame a few refreshes after its clock was read; the bitmap and bump effects poll the
  music once per drawn frame. The remake draws every refresh at its own time.
- **Dropped artefacts.** The tearing of the effects that draw straight into VGA memory, the previous part's frame
  shown in the next part's palette while it loads, the black refreshes at the start of the water, the 3D scenes and
  between the doomsday bump's two runs, the second 3D scene's frame with a black background, the 6-bit fade steps and
  the one-step shortfall of rising fades.
- **Smoother where the original quantises.** The bump mapping's water had a scratchy texture from its integer wave
  level; the remake's water is smooth. The bitmap effects' polar centres and squeezed areas, which the original
  aliases, show the real pattern or blur smoothly, and part II's two textures are blended instead of dithered. The
  water's and the landscape's relief is filtered, so they are less grainy than the original's nearest texels; where
  the ice bump's slopes are steep, its rock texture shows as continuous streaks instead of single texels. The
  Phong-lit blobs are on average about half a light-table row brighter than the original's rounded-down shade.
- **Kept as tables.** One bitmap table (`tab14`) and the three 2 × 2 tables have no exact formula; they are
  interpolated and keep their own noise. At the 2 × 2 swirl's centre a small disturbance about one original pixel
  wide remains.
- **Visibility.** Each heart and each 3D object resolves its own faces with a depth buffer instead of the original's
  face sort; objects are still drawn in the original's order.
- **Sound.** The remake models a real GUS (volume ramps from the current volume, the driver's wait for them); the
  capture's emulated card answered every volume read with the same value. The output is true mono, where the card's
  middle pan leans about 1.2 dB to the right.
