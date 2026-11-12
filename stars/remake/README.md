# STARS: Wonders of the World by NoooN (Assembly '95) — high resolution remake

A C++/OpenGL re-implementation of *STARS: Wonders of the World*, the MS-DOS demo by NoooN (music Groo, 3D motion and
vertices BARTi, system and special effects KARL, script and pixels RA, sound system LCA) that won the PC demo
competition at Assembly '95. It runs on Linux and Windows at any resolution in true colour, instead of 320×200 with
256 colours, with the original's motion, timing and music, and with high-resolution redraws of RA's pictures (or
the original pixels, `--original-art`).

The remake reads everything from the original `STARS.EXE` of the final release (1,224,140 bytes). It is not
included: unpack `nooonstf.zip` from scene.org into `stars/original/` (see [`../original/README.md`](../original/)).

| what | where in the exe | how it is used |
|------|------------------|----------------|
| the program image | an Infiny Loader–encrypted PKLITE 1.1x executable | decrypted and decompressed at start-up by `src/unpack.cpp` (as the loader stub and PKLITE's decompressor do it) and checked against the known release |
| the data | an overlay of 58 files, 56 DIET-compressed | unpacked by `src/unpack.cpp` (DIET as 0x4ad47) and checked file by file; pictures, palettes, maps, objects and tables are read by file number |
| the music | file 1 (a 16-channel MOD) | played by a port of LCA's MOD-Player V3.02 (`src/lcaplay.cpp`, with its period, sine, volume and pan tables from the image) through a model of the GUS GF1 (`src/gf1.cpp`) |
| the running order | main 0x4d | the music positions main waits for, as music times (`src/timeline.cpp`) |
| the 3D objects | files 18, 24, 28, 45, 46, 50, 51 (BARTi's `B!` and `WLDD` formats) | loaded by `src/stars/engine_objects.cpp`, with the parts' motion and morph records read from the image |
| the rest | tables at image addresses (sine tables, light curves, colour tables, the random seed, part constants) | read by address where the parts need them |

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
title bar. `--original-art` shows the original pixels of the pictures instead of the redraws. `--minute`: the
original reads the clock at start and on odd minutes swaps two name cards of the intro and the first two credits;
the remake shows the even-minute version (as in the reference capture) unless told otherwise.

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

`-DSTARS_PARTS="intro;cloud"` builds only some of the parts in `src/scenes` (development); the others show a
placeholder with the part's name, the music position and the time since the part started.

## How it is put together

- `src/main.cpp` options, playback loop, shots / video / bench; `src/demo.*` the framework (parts, the clocks,
  pictures, render-target pool, placeholders); `src/timeline.cpp` the running order; `src/player.*` playback and
  the clock (the audible position of the soundtrack)
- `src/exe.*`, `src/unpack.cpp` reading `STARS.EXE`; `src/pictures.*` palettes and pictures: the redraws in `art/`
  or the original pixels through their palettes
- `src/music.*`, `src/lcaplay.*`, `src/gf1.*` the soundtrack, rendered at load time
- `src/stars/` the core library: `core.h` (the random generator 0x4af10, the sine tables, the palette fader,
  palette textures), `engine.h` (BARTi's 3D engine on the GPU, used by the bee, the spike ball, the wall, the star
  and the torso)
- `src/scenes/` one port per part: `intro`, `cloud` (with `cloudfx.h`, shared with the bee's background), `bee`,
  `spikeball`, `warp`, `tunnel`, `wall`, `pictures` (the ornament and the credits), `star`, `particles` (both
  particle parts), `dissolve`, `torso`, `face`; `zoomblur.h` holds the feedback buffers of the particles, the
  dissolve and the face
- `art/` the redrawn pictures and how they were made (`art/README.md`); `tools/align_art.py` fits a redraw onto the
  original
- `tools/compare.py` puts remake frames next to the original's capture; `tools/test_music.cpp` checks the soundtrack
  (`cmake --build build --target test_music`); `tools/enginetest/` checks the 3D engine against an integer port of
  the original's transform and renders test scenes
- [`../re/PORT.md`](../re/PORT.md) the conventions every part follows; `../re/` the decompiled notes (pseudo-C with
  every constant), the emulator harness and the scripts that make the reference capture

## Fidelity

Every routine was decompiled, modelled in Python and checked against the original running in an emulator (see
[`../re/README.md`](../re/README.md)); the ports are compared with a capture of the original made in that emulator,
which runs the demo as a 486DX2/66-class machine does.

- **Timing.** The clock is the music: music time 0 is the moment the original's player starts the GUS timers, and
  the music runs on the GUS timer periods the player programs (1.17 % / 1.39 % faster than the module's nominal
  tempo). Main's script waits for music positions; the remake starts each part at the same position, or, where the
  original's set-up routines took longer than that, at the frame where the capture starts it (the bee, the wall and
  the two particle parts). Part ends are the parts' own tests (PIT ticks, positions, retraces). Where the original
  keeps the last frame on screen between two parts the remake does too (the cloud's last frame stays until the bee
  starts); where a set-up routine blacks the palette, the screen is black (after the spike ball).
- **Motion.** The parts' animation is stepped by the PIT (70.0635 Hz). The remake evaluates it continuously at the
  fractional tick, with exact sine and cosine instead of the 1023-amplitude tables, so motion is smooth at any
  refresh rate. Integer counters that the original steps tick by tick (the star's beats, the bee's keys, the
  spike ball's path) are replayed exactly and interpolated.
- **Frame-rate dependent state.** What the original advances once per drawn frame runs at the capture's frame rate
  in that phase and is blended between steps: the intro's band phases and card fades (per retrace, 70.09 Hz), the
  warp's oscillator and picture cycling and the tunnel's speed ramp (35.04 Hz), the particles' and the face's
  feedback (70.09 Hz), the dissolve's feedback (35.04 Hz). Values that the capture shows at a particular frame (the
  wall's fade-in ending at 63/64, the torso's rise ending at tick 158, the bee's text blink, the spike ball's path
  stalls, which drawn frame of the particles sees which tick) are taken from the capture.
- **Randomness.** The random generator 0x4af10 is replayed from its seed in the image, so the particles and their
  words appear where and when they do in the original.
- **Music.** The player's GUS register writes, the GUS memory after the upload and the music position after every
  tick match the capture exactly (`tools/test_music.cpp`: 144,716 register writes, 1 MB of GUS memory, 16,871
  ticks). The GF1 model evaluates the voices at the output rate (48 kHz) instead of the card's 19,293.75 Hz with 32
  active voices, with the card's pitch, volume ramps and timing; a 5 Hz high-pass stands for the card's AC-coupled
  output (one-shot samples that end on a non-zero byte would otherwise leave a constant offset), and the mix is
  normalised to 90 % of its peak (the player sets no master volume).
- **Pictures.** The hand-drawn pictures are shown as faithful high-resolution redraws by default (`art/README.md`
  says how each was made and how closely it fits the original); `--original-art` shows the original pixels as sharp
  squares with anti-aliased edges. Palette fades are colour multiplies, so they apply to both. Picture 13 (the
  third pose of the woman) is redrawn too (made on Comfy Cloud, as ChatGPT refused it) and shown in the intro and
  the warp; the dissolve still stamps its original pixels (only the redraws of 11 and 12 are masked for it). The
  bee's background (file 27) is a still of the cloud effect: with the redraws on it is rendered by the cloud effect
  at the offsets that reproduce it.
- **Sharper.** Effects are evaluated per output pixel (the intro's flower, the cloud tunnel, the warp's patch, the
  voxel tunnel's rays); textures and maps are read filtered and their colours looked up in true colour; 6-bit
  palette products and 8-bit levels are continuous; the feedback effects run at 3 or 4 times the original
  resolution; 4-neighbour averages are Gaussians of the same variance.
- **3D engine** (`src/stars/engine.h`). Vertices are transformed and projected in float with the original's
  matrices and projection, rasterised with 4× MSAA. The env coordinates of the vertices (128 + 63.75 n.xy, or
  127.5 n.xy for the torso's mirror) are interpolated across each face as the original's fillers do, but
  perspective-correct; texture coordinates too, both the short way round the map like the fillers' 8-bit
  differences. The light maps of 0x7873a are the light curve at the continuous radius; the bump offsets are read
  filtered and the texels' colours filtered in true colour. A depth buffer orders the faces, except in the bee,
  whose objects overlap themselves and need the file order the original draws them in.

## Checks

Numbers from the final check (Linux, i7-13700H, music time in seconds):

- **Against the capture**: contact sheets of the remake (both art modes) next to the capture every 8 s over the
  whole demo (`tools/compare.py`), and of every part boundary. Mean absolute difference to the capture
  (320×200, 0..255) with the original pixels: 0.1–2.5 for the pictures, 4–11 for the cloud and the tunnel, up to 37
  for the 3D, warp and feedback parts, where most of it is the remake's lead of 30–70 ms (see below), the sharper
  edges and the particles' glow.
- **Seeking**: 16 times across all parts rendered in ascending and in descending order give identical frames
  (maximum difference 0), and the frames of the particles, the dissolve, the face and the second particle part are
  identical whether reached by playing 30 frames at 60 fps or by a jump.
- **Judder**: 24–48 consecutive frames at 60 fps in each moving part change smoothly from frame to frame (no
  repeated or alternating frames). The dissolve's change per frame varies by about ±25 % because it cross-fades its
  35 Hz feedback steps, and a new stamp of the picture fades in over one frame.
- **Speed**: GPU time per frame at 2560×1920 over each whole part (`--bench`), median / 95th percentile in ms. The
  first frame of a part allocates its buffers (up to 75 ms).

  | part | Intel Iris Xe (integrated) | GeForce RTX 4060 Laptop (Mesa zink on NVK) |
  |---|---|---|
  | intro | 1.34 / 1.67 | 0.40 / 0.49 |
  | cloud | 1.43 / 1.94 | 0.52 / 0.61 |
  | bee | 2.59 / 2.88 | 3.14 / 3.43 |
  | spike ball | 2.42 / 3.59 | 2.94 / 3.60 |
  | warp | 1.15 / 1.39 | 0.50 / 0.61 |
  | tunnel | 4.32 / 4.52 | 2.67 / 2.94 |
  | wall | 3.64 / 3.96 | 3.44 / 3.71 |
  | ornament | 0.72 / 0.75 | 0.40 / 0.48 |
  | star | 2.67 / 3.14 | 5.02 / 5.91 |
  | particles | 1.29 / 1.57 | 0.75 / 0.97 |
  | dissolve | 1.49 / 1.87 | 0.72 / 0.95 |
  | torso | 2.87 / 3.28 | 3.72 / 4.64 |
  | particles 2 | 1.49 / 1.88 | 0.75 / 0.95 |
  | face | 5.43 / 6.08 | 2.04 / 2.25 |
  | credits | 0.75 / 0.83 | 0.31 / 0.43 |

- **Start-up**: ready in 330 ms (unpacking `STARS.EXE` 15 ms, the soundtrack 145 ms, the parts and the pictures
  the rest); 360 ms from the start of the process to the first frame written.
- **Builds**: g++ 16 and MinGW (x86_64-w64-mingw32) build without warnings (`-Wall`). `--wav` writes the 372.1 s
  soundtrack (48 kHz, peak −0.9 dBFS) in 0.4 s; `--video` renders a range with sound through ffmpeg.

## Known differences

- **The start-up** (PMODE's messages, loading and the precalc, about 4 s of black) is skipped: the remake starts
  with the music.
- **What a part shows at t.** The original draws a frame from the state at the moment it starts drawing and shows it
  one or two retraces later (the warp and the tunnel draw at 35 fps straight into video memory, so the capture also
  shows half old, half new frames); the remake shows the state at t. Against the capture the warp and the tunnel are
  30–45 ms ahead, the bee, the spike ball and the torso about 50 ms, the star 60 ms and the wall 70 ms.
- **Part ends** that the original tests once per drawn frame come up to one drawn frame later there (the wall ends
  4 frames later in the capture); the remake ends on the tick or row.
- **Brightness.** Where the original truncates (the DAC's `(p * L) >> 8` in every fade, the light levels `c >> 1`
  and `c >> 2`) the remake's continuous values lie half a step higher on average: dark colours in fades are up to
  one 6-bit step (4 / 255) brighter, and the 3D parts are 0.5–3 / 255 brighter than the capture on average.
- **Intro.** The flower texture, the petal outline and the radial offset are evaluated per pixel instead of along
  4096 integer rays; the band brightness and the card fade are continuous instead of 8-bit products of 6-bit
  colours. The band phases run on from card to card by the number of drawn frames, which depends on where in a frame
  each card starts: the remake counts with the average, so a card can be one drawn frame (1/41 of a pulse) off. The
  one-frame lag of the radial offset (14 ms) is dropped.
- **Cloud.** Near the centre, where a whole texture row wraps round a few pixels, the texture's large-scale
  variation shows as soft spokes. Layer 2 is read per pixel instead of once per pixel pair, and the offsets move
  continuously instead of in whole texels. Dropped: the 2x2 centre of stale memory and the undrawn rows 0, 198,
  199.
- **Bee.** The frame after the cloud is the last cloud frame with its palette doubled, the evident intent (the
  original doubles uninitialised memory there and shows garbage colours for one frame). The whole frame is drawn
  (the original leaves rows 0, 198, 199 dark purple). The text "23806 faces" blinks every 7 drawn frames in the
  original; the remake shows it during the ticks the capture shows it (at 25.6 fps).
- **Spike ball.** The DAC keeps the last fade-in level the loop sent (62 at the capture's 32.5 fps) until tick 1604;
  the remake uses 63 / 64 throughout. The path stalls for the ticks of every 256th drawn frame (a byte counter):
  replayed as the capture shows them (2, 3 and 2 ticks). The faster rotation starts on its tick (the original: one
  tick late, at the next drawn frame).
- **Warp.** Blocks 2–4 start on the tick at which the block before ends; the capture starts each up to one drawn
  frame later (about 1 tick each). The patch is evaluated in floats (the original's packed 8.8 sums are off by up to
  3.6 texels) and the pictures are read with a filter. Dropped: the tearing and the race between the PIT and the
  curve routine (62 of 915 frames mix two phases).
- **Tunnel.** The maps are read bilinearly at the continuous hit depth, so near the camera, where one map column
  covers about 10 original pixels, the original's flat streaks become smooth gradients and sharp steps in the colour
  map (the darker band at columns 240–0) become soft. The forward motion is continuous (the original moves in whole
  texels). Kept: steps 0 and 1 read the same column, step 0 without the twist.
- **Wall.** The shape is evaluated at the same tick as the motion (the original morphs after drawing, so its shape is
  one drawn frame behind). The two grey-level textures are blended continuously through the original's eight
  crossfade frames and coloured afterwards. Kept from the capture: the fade-in stops at 63/64 and stays there until
  the crossfade, and the crossfade triangle starts at tick 1080 (the first drawn frame at or after tick 1079).
- **Star.** The callback's state is stepped tick by tick and interpolated, so the pulses and the bounce fade are
  smooth; the race that mixes two ticks' palette levels in 6 frames of the capture is dropped. The carousel is
  rendered live at the continuous frame t / 2 instead of 64 pre-rendered frames at 35 fps (object 0's left-over angle
  and the cut at row 78 are kept).
- **Torso.** Kept from the capture: the rise is updated per drawn frame and its last update there was at tick 158,
  so the torso stays 22.9 units low and spins from angle 43. The mirror phases start exactly at ticks 736 and 1600
  (the original: at the first drawn frame from then). The reflection map is rendered at 1024x1024. The ring's env
  index is 0x60 + 31 v / 63 so that the filtered map stays on the red ramp.
- **3D engine** (bee, spike ball, wall, star, torso). Projection with exact sin / cos (the original's 1023-amplitude
  table makes objects 0.1–0.3 % smaller) and MSAA instead of integer corners. The light curve at the continuous
  radius with continuous levels replaces 16 / 32 isqrt rings, and the filtered texture and bump offsets make the
  original's texel noise on the spike ball and the wall smoother. Faces that come nearer than 2F to the eye are
  clipped at that plane instead of disappearing whole (visible as the star flies into the camera). Where the parts
  use a depth buffer the order of faces is the geometric one instead of the painter's sort (no visible error of the
  sort was found in the capture).
- **Particles** (`src/scenes/particles.cpp`, `zoomblur.h`). The feedback runs on a float level buffer at 3 times the
  original resolution: the zoom is the continuous map the integer maps follow on average (15/16 about (164.0, 93.1))
  instead of 4-pixel groups, so the static 16-pixel cross at the centre of the original's maps (a vertical seam; the
  glow stays longer there) is gone, and the centre of the glow fades sooner than in the capture at some moments
  (e.g. 316 s); the 4-neighbour average is a Gaussian of the same variance (plus the 1/12 px² of the original's
  nearest reads); the `>> 2` truncation is a loss of up to 3/8 level per step where the neighbours differ (it keeps
  the mean level within a few per cent of the integer model). The blobs are anti-aliased ellipses of the same area,
  the words file 58's lettering as a smooth mask, the 32-colour ramp continuous. Which drawn frame sees which PIT
  tick follows the capture's frame timing (`CaptureClock`), so the words are on the same frames as in the capture.
- **Dissolve.** Float RGB at 3 times the resolution, stamped with the redraws of 11 and 12 masked like the original
  (decided per pixel of the redraw near the outline of files 36 / 37) and file 13's original pixels. The G / R-B
  checkerboard is shown as true colour at 0.73 of the planes, the light the eye mixed from it (half the light:
  0.5^(1/2.2) in display values), so the remake is about 1.46 times as bright as the capture averaged per pixel.
  The 35 Hz steps of the capture are kept (the original's speed depends on the machine).
- **Face.** The index feedback runs at 4 times the resolution with nearest reads, so the remap tables of file 35
  still decide how the trails fade; the screen interpolates the colours of four index texels and draws the head at
  the output resolution (its map coordinates interpolated, the map read as a continuous level of the ramp). The warp
  is evaluated in float with the mean of the original's floors. Dropped: the column carry of 0x11a0a (one map texel
  on half of the head) and the 132 pixels at the end of the screen that are not remapped.
- **Per-frame feedback** (particles, dissolve, face): the screen blends the frames before and after t, so a new
  frame (a stamp, a strobe flash) fades in over one frame instead of appearing at once. After a seek the feedback is
  replayed from the last checkpoint (every 48 or 64 frames; about 110 MB of GPU memory for the four parts).
