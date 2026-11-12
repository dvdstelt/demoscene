# STARS remake — shared brief for everyone writing code in `stars/remake`

*STARS: Wonders of the World* (NoooN, 1995, MS-DOS; music Groo, 3D motion and vertices BARTi, system and special
effects KARL, script and pixels RA, sound LCA) is remade in C++20 / SDL3 / OpenGL 3.3 core. The goal: **the same as
the original, but much sharper**. Same motion, timing, composition, colours and music as the original, rendered at
any resolution in true colour, with faithful high-resolution redraws of RA's pictures (or the original pixels with
`--original-art`).

The original has been reverse engineered. Read the notes before writing code:

- `stars/re/NOTES.md` (file layout, PMODE, the three clocks, LCA's player), `PARTS.md` (the part timeline with
  frames, positions, frame rates and what depends on the frame rate);
- `stars/re/notes-*.md`: `core` (main's script, the fader, the random generator, the overlay files, the pictures),
  `music`, `engine` (BARTi's 3D engine), `parts2d-a` (intro, warp, tunnel), `parts2d-b` (particles, dissolve, face),
  `parts3d-a` (precalc 0x16945, cloud, bee, spike ball), `parts3d-b` (textured surface, chrome star, torso). Every
  routine is pseudo-C with exact constants, checked against the emulator; the Python models in
  `stars/re/models/<scope>/` are a good source of exact reference values;
- `stars/remake/art/README.md` (the redraws), `AGENTS.md` in the repository root (the rules).

## Fidelity rules

- Keep: timing, motion paths, camera, composition, colours, random sequences (replay the generator 0x4af10 from its
  seed, `stars::Random`), the minute variant (`ctx.minute`) and the visible quirks that are part of the look (each
  notes file lists them, and says which ones a remake may drop).
- Evaluate exactly instead of copying low-resolution shortcuts: lookup tables (the sine tables, the engine's COS,
  projection and radius tables) become continuous float maths; 6-bit colours, 8-bit levels and integer palette
  products become float; nearest-neighbour texture reads become filtered reads; integer rays, columns and spans
  become per-pixel evaluation; polygon edges are anti-aliased (MSAA or analytic coverage); box blurs become
  Gaussians of the same variance at the output resolution.
- Drop artefacts that only exist because of 320x200 / 8 bit (truncation steps, aliasing, palette quantisation,
  carries between packed pixels), but never change what a viewer would describe as the content.
- Nothing from STARS.EXE may be copied into the source: read every table, palette, picture, object and string at run
  time through `StarsExe` (`src/exe.h`): `c.exe->u8/u16/s16/u32/s32/bytes(addr)` (image addresses as in the notes)
  and `c.exe->file(n)` (overlay file 1..58, unpacked, as 0x4a9f3 returns it). Constants that are code (multipliers,
  loop counts, addresses, offsets, format constants) may appear in the source.

## Time

**The clock is music time.** `render(ctx, t)` gets `t` = seconds since the player started the GUS timers
(0x1c759 → 0x1a91c, called by main after its precalc: program time 3.98976 s, capture frame 279). The music agent's
`Soundtrack` (`src/music.h`) uses the same zero: player tick 0 at `startTime` = 0.02048 s, pcm frame 0 at t = 0.
The demo ends at t = 376.49 s (text mode after the last fade); the music stops at 372.11 s.

Why music time: the music runs on the GUS crystal, main's script waits on music positions, and the parts start at
those positions; the PIT and the retrace are free-running clocks with no fixed phase to the music, and the parts only
count them from their own start. So everything is expressed as music time, and the other clocks are derived:

| clock | rate | in the remake |
|---|---|---|
| music position (0x29f80: order << 8 \| row, the *next* row to be read) | GUS timer: 40.064 ticks/s (tempo 99, orders 0..0x0a), then 46.64 ticks/s | `c.position(t)`; `c.positionTime(pos)` = first time position >= pos; `c.afterPos(x)` = when `cmp ax, x; jbe` ends (AX > x); `c.tickTime(k)` = player tick k. Exact (the music port's tick times) |
| PIT callbacks (IRQ 0) | 1193182 / 0x4286 = **70.0635 Hz** (`kPitHz`) | `c.ticks(t)` = PIT ticks since the part started, continuous. The original's counter n is its integer part (the first tick falls 0..1 tick after the install: use `floor(c.ticks(t))` where the original compares n with a threshold, the continuous value for motion) |
| vertical retrace (0x4a830) | 25.175 MHz / (800 x 449) = **70.0863 Hz** (`kRetraceHz`) | `c.retraces(t)` = retraces since the part started. main's fades and the per-frame parts step on it |
| drawn frames | 70.0863 / n in the capture (PARTS.md "fps") | where the original advances state once per drawn frame, step it at the capture's rate (70.0863, 35.04 = 70.0863 / 2, ...) with `TickStepper` |

Both `ticks` and `retraces` have an overload `(t, since)` for phases that start later than the part.

**Part starts and ends** (`src/timeline.cpp`, main's script 0x4d, notes-core section 2). A part starts at the later
of its music position and the moment main got there (the end of the part before plus the set-up routines in
between). Ends are the parts' own end tests (tick counts, positions, retraces) and exact; the set-up times depend on
the CPU and come from the capture (`notBefore(frame)`). `c.start` / `c.end` are the current part's start and end;
take the part's tick 0 at `c.start`. Between a part's return and the next part the screen keeps the last frame where
the original did not clear it (`Cue::hold`: render() is called with t just before `c.end`). End tests that the
original evaluates once per drawn frame fire up to one drawn frame later there (e.g. the wall ends 4 frames later in
the capture); the remake ends on the tick. `stars-remake --cues` prints the running order with the capture's frames:

| scene | routine | start (s) | end (s) | capture frames | job |
|---|---|---|---|---|---|
| `intro` | 0x5a92 | 0.000 | 70.388 | 279–5212 | parts2d-a |
| `cloud` | 0x126ae | 73.364 | 80.786 (held to 80.863) | 5421–5941 (5947) | parts3d-a |
| `bee` | 0x11e7f | 80.863 | 96.106 | 5947–7015 | parts3d-a |
| `spikeball` | 0x6534 | 98.324 | 131.213 (held to 131.256) | 7170–9475 (9478) | parts3d-a |
| `warp` | 0x975b | 131.256 | 157.419 | 9478–11312 | parts2d-a |
| `tunnel` | 0x11e8 | 160.071 | 192.960 | 11498–13803 | parts2d-a |
| `wall` | 0xa0ca | 193.053 | 223.839 | 13810–15967 | parts3d-b |
| `ornament` | main (picture 57) | 223.839 | 233.482 | 15967–16643 | parts2d-b |
| `star` | 0x164dc | 233.482 | 259.259 | 16643–18450 | parts3d-b |
| `particles` | 0x133ad | 259.271 | 267.057 | 18451–18996 | parts2d-b |
| `dissolve` | 0x1535c | 267.100 | 273.789 | 18999–19468 | parts2d-b |
| `torso` | 0x16f7c | 274.304 | 309.229 | 19504–21952 | parts3d-b |
| `particles2` | 0x133f1 | 309.281 | 330.348 | 21956–23432 | parts2d-b |
| `face` | 0x1089c | 330.391 | 355.047 | 23435–25163 | parts2d-b |
| `credits` | main | 355.047 | 376.491 | 25163–26666 | parts2d-b |

Black between the cues. If your part's end or start in the notes differs from this table, report it (timeline.cpp is
a core file).

**Per-frame state.** What the original advances once per drawn frame (the intro's band phases and card fades, the
warp's oscillator and picture cycling, the tunnel's speed ramp, the particles' and the dissolve's feedback, the
face's warp, remap and strobe, the bee's text blink, ...) runs at the capture's frame rate in that phase (the notes
give it: 70.0863 Hz or 35.04 Hz). Step it exactly as the original with `TickStepper`
(`steps = c.retraces(t) / n`) and blend continuous quantities between step k and k + 1 by the fraction, so motion is
smooth at any refresh rate. Sudden changes stay on their step. Where a PIT-driven value is sampled once per drawn
frame (most 3D parts), evaluate it continuously at t.

**Seeking.** `render(ctx, t)` must depend only on `t`: players seek, and shots and videos render times in any order.
Cheap stepped state is replayed from the part start on every frame (`TickStepper` resets after a backwards seek).
Feedback buffers on the GPU (particles, dissolve, face) are kept in the part (its own `Target`s, not the pool),
advanced step by step while playing forwards; after any other jump they are rebuilt by simulating the last N steps
from the cleared buffer, with N large enough that what came before contributes less than 1/1000 (or from the part
start with checkpoints if the state never forgets). Check: the frame at a time must be the same after seeking
forwards, after seeking backwards and while playing.

## Screen and colour

- The original's 320x200 screen is shown at 4:3. `ctx.out` is a 4:3 RGBA16F target of any size (bound, cleared to
  black). "VGA coordinates": x 0..320, y 0..200, top-left origin; the original pixel (x, y) covers [x, x+1] x
  [y, y+1], its centre is (x + 0.5, y + 0.5); one original pixel is 1.2 times as tall as wide on screen.
  `ctx.vgaProj()`, `ctx.drawTexture(...)`, `ctx.rect(...)`, `ctx.drawPicture(...)` work in them.
- Colours are display-space floats 0..1 (no sRGB conversion). A 6-bit VGA value v is v / 63; an 8-bit value v / 255.
  `vgaPalette(bytes, first)` (`src/pictures.h`) turns DAC bytes into a `Palette` (256 `Vec3`).
- **Fades** (0x4a720: DAC = min(63, (p * L) >> 8), L = 0..256) are a colour multiply with clamp in display space:
  `colour * L / 256`, at most 1 (`fadeColour`, `fadeMul`, `PictureDraw::mul`). So they work on redraws and on
  anything rendered in true colour. `stars::Fade` gives L(t) for main's fades (`mainFade(c.afterPos(x), from, step,
  n)`: one step per retrace from one retrace after tc) and for the parts' PIT-stepped fader (`Fade{c.start, kPitHz,
  0, 4, 64}`). Other palette operations of the notes (fade from white in the bee, blends between two palettes,
  flashes towards white) are per-channel affine maps too: `mul` and `add` of `PictureDraw`, or your shader.
- **8-bit index effects in true colour**: the original computes palette indices and the DAC turns them into colour.
  Do the colour step where the index is made, in float:
  - the index is a level of a ramp (shading `base + shade`, env-map intensities, the tunnel's colour map, file 29's
    64 levels): compute the level continuously and interpolate between the ramp's colours (`stPalLerp`), or evaluate
    the ramp's formula;
  - the index combines layers (the cloud's `16 a + b` through a 16x16 palette): keep the layers as separate
    intensity maps and look the combination up bilinearly in a 16x16 colour table;
  - palette cycling / pulsing (the intro's flower bands): rebuild the palette per frame in float
    (`PaletteTexture::set`) and look indices up with `stPal`, or evaluate the band brightness in the shader;
  - remap tables on indices (the face's 32 tables, file 35): these define the look; run them in index space at the
    resolution you choose and convert at the end, or find a continuous equivalent, and say which in the README.
  `stars::kGLSL` has `stPal`, `stPalLerp`, `stSinA`.
- The 3D parts' backgrounds and frames cover rows 1..197 only (0x78905); rows 0, 198, 199 stay black.

## Pictures and the art switch

`stars/remake/art/fileNN.png` are faithful redraws of the hand-drawn files (see its README: 5–12, 14, 19, 25, 32,
47, 52–57 so far). The remake shows them by default; `--original-art` shows the original pixels. A part never needs
to know which:

```cpp
const Picture& p = c.picture(57);                    // in init(): the redraw, or the original through its palette
c.drawPicture(p, {.mul = fadeMul(level)});            // full screen, faded (premultiplied blending, stays set)
c.drawPicture(c.picture(5), {.x = 88, .y = 150, .w = 144, .h = 36, .sy = 36.0f * k, .sh = 36});  // sprite k
Palette pal = vgaPalette(c.exe->fileSpan(15));       // a screen without its own palette:
const Picture& bg = c.picture(19, {.palette = &pal}); //   colours for the original pixels (redraws ignore them)
c.drawPicture(bg, {.y = 1, .h = 197, .sy = 1, .sh = 197});
```

- `c.picture(n, PictureOptions)` returns a cached texture covering the file's whole pixel grid (`Picture::srcW x
  srcH`: 320x200 for screens, 144x144 for the four words of file 5, 128x296 for the names of file 6, 256x256 for
  file 7 and the maps, 80x36 for file 32). Options apply to the original pixels only: `palette` (default: the file's
  own palette; 5 / 6 / 7 place theirs at DAC 1 / 0x60 / 0x10; a grey ramp for files without one), `key` (a
  transparent index; default 0 for files 5 and 6), `opaqueBelow` (default 0x50 for file 32), `indexAdd` (file 47:
  0xe6). Textures hold premultiplied alpha.
- `PictureDraw` (`demo.h`): destination rectangle in VGA coordinates, source rectangle in the original's pixels,
  `mul` / `add` (colour = clamp(c * mul + add, 0, 1)), `alpha`. Original pixels are drawn as sharp squares with
  anti-aliased edges, redraws filtered.
- Pictures used as textures by an effect (the warp samples 11..13, the intro's flower is file 7): bind
  `p.tex` in your own shader, sample with `texSharp` (`kCommonGLSL`) if `!p.redraw`, `texture` otherwise, and
  remember the premultiplied alpha. Where the effect needs the original's indices (bands of file 7, masks, levels),
  read `c.exe->file(n)` and build your own textures; the art README says what a redraw keeps.
- `c.art()` tells whether redraws are on; `--picshots 57,5 --shotdir DIR` draws pictures to PNG for checking.

## Layout

- `src/main.cpp` program, options, playback, `--shots` / `--video` / `--wav` / `--bench` / `--cues` / `--picshots`;
  `src/timeline.cpp` the running order; `src/demo.*` framework (`Scene`, `REGISTER_SCENE`, `Ctx`, `TickStepper`,
  `stepsAt`, target pool, the placeholder); `src/render.*`, `src/gl.*` GL helpers (`Shader`, `Texture`, `Target`,
  `Mat4`, `Vec3`; `kCommonGLSL` is prepended to every fragment shader: hash12, rot2, texBicubic, texSharp, luma);
  `src/exe.*` + `src/unpack.cpp` STARS.EXE (Infiny Loader, PKLITE, DIET; checked against the release by hash, 30 ms);
  `src/pictures.*` palettes and pictures (the art switch); `src/player.*` playback and the clock; `src/music.*`,
  `src/lcaplay.*`, `src/gf1.*` the soundtrack (music agent).
- `src/stars/` the core library shared by several parts: `core.h` (random generator 0x4af10, the sine tables
  0xa740 / 0x14260 / 0xffb8 / 0x5599c, `Fade`, `PaletteTexture`, `kGLSL`); the 3D engine goes in
  `src/stars/engine*`. Its headers say what each part can use.
- `src/scenes/<part>.cpp` one file (or a few) per part, named as the scenes in `src/timeline.cpp`. Code shared by the
  parts of one job goes in a header in `src/scenes/` (e.g. `zoomblur.h` for the particles and the dissolve).
- `tools/compare.py` side-by-side check against the capture; `tools/align_art.py` the redraws' alignment;
  `tools/test_music.cpp` the music checker.

Part agents do not edit core files (`src/*.cpp`, `src/stars/*`, `CMakeLists.txt`; others build on them at the same
time): work around a problem in your own files and report it. Core agents do not edit scene files.

## Building and checking (inside the toolbox container "acme303")

Use your own build directory, and build only your own part files:

```
cd stars/remake
toolbox run -c acme303 cmake -S . -B build-<you> -G Ninja -DSTARS_PARTS="cloud;bee"
toolbox run -c acme303 cmake --build build-<you>
toolbox run -c acme303 ./build-<you>/stars-remake --shots 75.5,79.25 --shotdir <scratch> --shotsize 960x720
toolbox run -c acme303 python3 tools/compare.py <scratch>/cmp.jpg --shots <scratch> 75.5 79.25 [--crop x,y,w,h] [--score]
```

Then look at `cmp.jpg` with the Read tool: left the capture record on screen at that time (labelled with its frame,
music position and main's routine), right the remake. `STARS_PARTS` lists basenames in `src/scenes`; parts that are
not built show a placeholder (name, position, seconds and PIT ticks since the part start, the capture frame). The
program finds `stars/original/STARS.EXE` and `stars/remake/art` by itself. `--original-art` compares against the
original pixels (closest to the capture); `--minute odd` shows the other variant. `--video out.mp4 --from T0 --to T1`
renders a range at 60 fps through the host ffmpeg (with the music); `--bench T0,T1 --shotsize 2560x1920` reports the
GPU time per frame; `--window --start T --info` plays from T with the time, position and part in the title.
Other people build in this tree at the same time: if the build breaks in a file that is not yours, wait a minute and
retry; never edit someone else's file to fix it — report it in your final message instead. Delete your build
directory when you are done.

The capture (`stars/re/full.frames`, see `stars/re/README.md` and `BRIEF.md`: per displayed frame at 70.0863 Hz the
320x200 page and the 6-bit DAC palette; `full.ticks` gives each frame's program time, PIT and player ticks, position,
main's routine) shows the original as a 486DX2/66-class machine draws it: compare composition, motion, colours and
timing, not single pixels. Music time t is capture frame `floor((t + 3.98976) * 70.0863)` (`Ctx::captureFrame`).
The snapshots in `stars/re/snaps/` and the models in `stars/re/models/` give exact intermediate values (tables,
buffers, per-frame state).

## Porting jobs

| job | files | scenes / scope | notes | depends on |
|---|---|---|---|---|
| engine | `src/stars/engine.h`, `src/stars/engine*.cpp` | BARTi's engine on the GPU: the `B!` (files 18, 24, 45, 46, 50, 51) and `WLDD` (28) loaders, morphing meshes, object and scene structures, rotation (0x7984e) and projection, the pixel modes the parts use (env map, texture + bump-offset env mode 15, modes 2, 3, 20, 21) as shaders that compute the colour in float from the maps and palettes (the fillers' colour rules: what an 8-bit index meant, see "Screen and colour"), depth buffer + MSAA instead of the painter's order where nothing visible depends on it, presenting into rows 1..197 over a background. An API the 3D parts call with their own motion; a checker against the snapshots (`tools/enginetest/`) | `notes-engine.md`; how the parts call it: `notes-parts3d-a.md` section 6, `notes-parts3d-b.md` section 2 | core |
| parts2d-a | `src/scenes/intro.cpp`, `warp.cpp`, `tunnel.cpp` | `intro` (title pictures 9, 8, 11, 12, 13, 14, 10 with their fades, the name cards: flower file 7 through the polar map with the band pulsing, words file 5, names file 6, the minute variant), `warp` (pictures 11..13 as textures, the patch), `tunnel` (voxel tunnel, maps 2, 3, palette 4) | `notes-parts2d-a.md`, notes-core 14 | core |
| parts2d-b | `src/scenes/particles.cpp` (scenes `particles`, `particles2`), `dissolve.cpp`, `face.cpp`, `pictures.cpp` (scenes `ornament`, `credits`) | the particles with the zoom-blur feedback and the words of file 58 (random generator replayed), the dissolving women (files 36..41), the face (files 34, 35, the remap tables), main's picture 57 and the credits (52..56, 10; the final fade) | `notes-parts2d-b.md`, notes-core 2.2 and 3 | core |
| parts3d-a | `src/scenes/cloud.cpp`, `bee.cpp`, `spikeball.cpp` | `cloud` (two-layer cloud tunnel, file 30, palette 31), `bee` (file 28, env map 29, background 27, the "23806 faces" sprite 32, fade from white), `spikeball` (file 18, maps 16, 17, background 19, palette 15) | `notes-parts3d-a.md` | engine |
| parts3d-b | `src/scenes/wall.cpp`, `star.cpp`, `torso.cpp` | `wall` (file 24, the morphing grid, textures 21 / 23, background 25, palettes 20 / 22), `star` (file 51, env map 49, the palette flashes, the carousel of file 50 that the precalc 0x16945 prepares: notes-parts3d-a section 5), `torso` (files 45, 46, maps 29, 43, 44, background 47, palette 42, the reflection) | `notes-parts3d-b.md` | engine |

The engine and the two 2D jobs can run in parallel; the 3D jobs start when the engine's API is in place.

## Quality

- Smooth at 60+ fps at 2560x1920 on a mid-range GPU; all initialisation of all parts together under 2 s (the exe,
  the soundtrack and the pictures take about 0.4 s now).
- Anti-aliased edges (analytic coverage in shaders, or MSAA for geometry).
- Portable C++20 (g++, MinGW, MSVC), no new dependencies. Every file starts with a comment saying what it draws or
  provides and which original routines (by address) it ports. Small readable functions.
- Write down in `stars/remake/README.md` ("Known differences") what cannot be exact, and in the scene file what was
  evaluated exactly instead of the original's shortcut.

## Your final message

Under 300 words: what you built (files, public API), how you verified it (compare sheets, numbers), what differs
from the original and why, and what is left.
