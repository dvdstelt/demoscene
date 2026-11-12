# DOPE remake — shared brief for everyone writing code in `dope/remake`

*DOPE* (Complex, The Gathering '95 PC demo; code jmagic, graphics and music jugi, boxes pixelled by reward, "media
labs" logo reward) is remade in C++20 / SDL3 / OpenGL 3.3 core. The goal: **the same as the original, but much
sharper**. Same motion, timing, composition, colours and music as the original, rendered at any resolution in true
colour, with faithful high-resolution redraws of the hand-drawn pictures (or the original pixels with
`--original-art`).

The original has been reverse engineered. Read the notes before writing code:

- `dope/re/NOTES.md` (file layout, the RNC packer, the loader and its player, CDPI, the parts' common structure, the
  retrace timing, video modes), `PARTS.md` (the part timeline with frames, music positions, drawn frame rates and
  routines);
- `dope/re/notes-*.md`: `core` (start-up, CDPI, the per-part framework and its retrace timing, p1 completely, the
  hand-overs between parts, the three fades of section 5.2, the global schedule, the pictures), `engine` (the 3D
  engine of p2, p3, p5, p6, p7), `p2`, `p3p5`, `p4`, `p6p7`, `music`. Every routine is pseudo-C with exact
  constants, checked against the emulator; the Python models in `dope/re/models/<scope>/` reproduce captured frames
  and tables bit for bit and are a good source of exact reference values;
- `dope/remake/art/README.md` (the redraws, being made now), `AGENTS.md` in the repository root (the rules).

## Fidelity rules

- Keep: timing (to the retrace), motion paths, camera, composition, colours, and the visible quirks that are part
  of the look (each notes file lists them under "quirks to keep", and says which ones a remake may drop). DOPE has no
  random numbers.
- Evaluate exactly instead of copying low-resolution shortcuts: the 2048-entry Q15 sine table becomes
  `sin(angle * 2 pi / 65536)`; integer matrices, projections and vertex positions become float; per-retrace integer
  steps are interpolated between retraces; nearest-neighbour texel reads (env maps, bump maps, the fire's cloud
  texture, the landscape) become filtered reads; the 4x4 dither and the 15 fire levels, the 32- and 64-step shade
  ramps and the 1/256 palette weights become continuous values; painter's order becomes a depth buffer with MSAA
  (unless the order itself is visible: the notes say where); 160-wide or 2-pixel effects become per-pixel.
- Drop artefacts that only exist because of 320x200 / 8 bit (truncation steps, carries between packed halves,
  16-bit overflows of the back-face test, never-drawn columns, tearing), but never change what a viewer would
  describe as the content.
- Nothing from DOPE.EXE or DOPE.MOD may be copied into the source: read every table, palette, picture, mesh and
  string at run time through `DopeExe` (`src/exe.h`, below). Constants that are code (multipliers, loop counts,
  retrace counts of the scripts, velocities the scripts set as immediates, addresses, offsets, format constants) may
  appear in the source. The scripts' parameters are immediates in the code: write them as constants with the
  address of the instruction they come from (or read them from the image by address, if you prefer).

## Reading the originals (`src/exe.h`)

`c.exe` is the unpacked release: `c.exe->part(n)` (n = 1..7), `c.exe->loader()`, `c.exe->term()` are the images
of `dope/re/unpacked/<name>.bin` byte for byte (unpacked from DOPE.EXE at start-up in 18 ms and checked by hash), and
`c.exe->mod()` is DOPE.MOD. Addresses are written as in the notes:

```cpp
const DopeImage& p2 = c.exe->part(2);
p2.u8(0x0451, 0x030a);              // 0451:030A (file offset SEG * 16 + OFF); also s8, u16, s16, u32, s32
p2.span(0x6507, 0x0010, 720);       // 720 bytes at 6507:0010 (std::span, no copy); bytes(...) copies
p2.cs16(0x1955);                    // [1955] = cs:[1955] = 0010:1955; also cs8, css16, cs32, css32, csSpan
p2.file16(0x4510);                  // "file 0x4510"
c.exe->loader().file8(DopeImage::code32(0x0b37));  // the loader's code32 offset 0xb37
```

Every accessor throws `std::out_of_range` outside the image. The images are the decoder output relocated for load
segment 0, so a segment value read from the image (far pointer tables, `push 0x4527`) is an image paragraph.

## Time

**The clock is music time.** `render(ctx, t)` gets `t` = seconds of the soundtrack (`src/music.h`): music time 0 is
the GUS timer overflow at capture program time 0.1104 s, after which main starts the player; player tick 0
(row 00:00) is at `c.tickTime(0)` = 0.01984 s. The framework anchors the schedule on that tick (capture program time
0.130240, `kCaptureFirstTick`), so music time = capture program time - 0.1104.

DOPE never synchronises pictures and music: the loader starts the player, then CDPI runs the seven parts one after
the other; each part installs a retrace-synchronised timer and counts the vertical retrace (70.0863 Hz) from its
own start, and its script is a list of "run N retraces" calls. Nothing in the parts reads the music, and the player
reads nothing from the parts. So the remake places the parts at fixed times after the first player tick, taken from
the reference capture (`dope/re/full.frames`: no disk time, 100 M instructions per second), and every part counts
retraces from its own start:

| clock | rate | in the remake |
|---|---|---|
| vertical retrace (each part's int 8 handler, its per-retrace routine) | 25.175 MHz / (800 x 449) = **70.0863 Hz** (`kRetraceHz`) | `c.retraces(t)` = per-retrace calls since the part's first one, continuous; `c.retraceTime(r)` the inverse; `(t, since)` overloads for later phases |
| drawn frames (the engine parts' frame loops, p4's loop) | the capture's (PARTS.md "drawn fps": 70 almost everywhere, p2 46..66 in its heavy shots, p7 41..56) | only p2's blend and credit fade advance per drawn frame: evaluate them per retrace (the notes give the continuous form) |
| music (GUS timer 2) | 22.40 ms per tick for orders 0..2, then 20.48 ms | `c.position(t)`, `c.positionTime(pos)`, `c.tickTime(k)`: for display only; no part may depend on them |

**Part starts and ends** (`src/timeline.cpp`, notes-core sections 5 and 6). `c.start` is the part's first
per-retrace call (its first int 8 handler run), `c.end` the end of its last frame, `c.load` (<= start) where it
first changes the screen (its set-up screen: `render` is called with `t < c.start`, `c.retraces(t) < 0`). Between
a part's end and the next part the screen keeps the part's last frame (`Cue::hold`: `render` is called with `t` just
below `c.end`) where the original's screen stays, and is black elsewhere. `dope-remake --cues` prints the running
order with the remake's and the capture's records:

| scene | load | start | end | held to | remake records (start..end) | capture records | between this part and the next |
|---|---|---|---|---|---|---|---|
| `p1` | 0.104 | 0.104 | 38.471 | — | 15..2704 | 15..2704 | black |
| `p2` | 38.599 | 38.599 | 142.942 | — | 2713..10026 | 2713..10026 | (p2 draws white for its retraces >= 7310, then p3's set-up screen) |
| `p3` | 142.942 | 143.027 | 194.464 | 194.664 | 10032..13637 | 10032..13637 | the plain nebula (p3's last frame) |
| `p4` | 194.664 | 194.664 | 300.676 | 300.819 | 13651..21081 | 13651..21081 | the plain chequerboard |
| `p5` | 300.819 | 300.819 | 365.681 | 365.796 | 21091..25637 | 21091..25589 | p6's swirl (the end of p5's hand-over) |
| `p6` | 365.796 | 365.796 | 423.253 | — | 25645..29672 | 25597..29598 | black |
| `p7` | 423.353 | 423.353 | 461.977 | — | 29679..32386 | 29605..32120 | the end of the demo (text mode) |

Times are music time (s). "Records" are the capture's frames (record f starts at program time f / 70.0863 s); the
remake uses the same grid, shifted after the three fades below. The p2 row assumes the capture's step starts
(notes-p2 9.2: steps 99..152 start up to 12 retraces late; use the measured starts, as the notes recommend for a
remake compared with the capture). If your part's start, end or set-up screen in the notes differs from this
table, report it (timeline.cpp is a core file).

**The three fades at hardware speed** (notes-core 5.2). p5's hand-over cross-fade to p6's swirl (3038: 65 retrace
waits), p6's exit fade (3d22: L = 256, 249, ..., 4) and p7's exit fade (33be: L = 256, 255, ..., 0) wait for the
retrace bit alone; on a real VGA that is one step per retrace, the emulator did about four per captured frame. The
remake takes one retrace per step, so it runs later than the capture from there on (by 48 records after p5's
hand-over, 74 after p6's exit fade, 266 at the end; notes-core 5.2 estimates 76 and 269 from the step counts, but
the black of p6 and the text mode of p7 are written right after the last step's DAC write, without another wait,
so they replace that step in its own record: 3d22 at 3d64, 33be at 3400). Step j of each fade is on screen in the
record r0 + j:

| fade | r0 (remake record) | in the part's retraces | step j | then, in the record of the last step |
|---|---|---|---|---|
| p5 hand-over | 25572 | `c.retraces(t)` = 4481 + j | the chequerboard / swirl mix with w = max(0, j - 1) * 64 / 4096 (j = 0, 1 both w = 0: first `mix(0)`, then the loop from 0) | j = 64: the swirl alone (DAC 0..15 = its palette) |
| p6 exit | 29636 | 3991 + j | L = 256 - 7 j (j = 0..35) | j = 36: black |
| p7 exit | 32130 | 2451 + j | L = 256 - j (j = 0..255) | j = 256: text mode, the end |

Interpolate the fades between steps (`dope::Fade`); `c.captureFrame(t)` maps the remake to the capture's records
(the capture's fades are compressed four to one), which `tools/compare.py` uses.

**What a frame shows** (the capture's records are taken at the end of their frame; the remake shows record f during
[f, f + 1) / 70.0863 s, and during the record of the (n+1)-th per-retrace call `c.retraces(t)` runs from n to n + 1):

- engine parts (p2, p3, p5, p6, p7): the 3D scene of record start + n is built from the state after n calls (it is
  built before call n + 1 and copied after it): use the state at `r = c.retraces(t)`. The label is drawn by the
  frame copy after call n + 1 with the position after it: use `r + 1` for the label.
- p1 and p4 change the screen in the handler itself (p1's start address, panning and DAC; p4's palette on odd
  IRQs, its frame loop draws continuously): record start + n shows the state after call n + 1: use `r + 1` (p1:
  `pos = 2 r` from the notes' "pos = 2 (k - 1) after the k-th run" is the same thing).
- the last frame: at `t` just below `c.end` (the held frame) clamp continuous motion to the last state the original
  drew (p3: state 3604, not an interpolation towards 3605).

**Per-retrace state.** Everything the per-retrace routines advance (object and camera motion, angles, the label,
p2's white fade, p4's fade A, phase and landscape position, p5's bump phase, p6's morph and bump strength, p7's
light counter) is a sum of per-retrace steps whose rates the script changes at known retraces. Evaluate it in
closed form at `c.retraces(t)` (`dope::Integrator`, `dope::LabelSlide`), exact at whole retraces and linear between
them; sudden changes (a value the script sets) stay on their retrace. For state that cannot be written in closed
form, step it with `TickStepper` (`steps = c.retraces(t)`) and blend by the fraction.

**Seeking.** `render(ctx, t)` must depend only on `t`: players seek, and shots and videos render times in any order.
Closed forms need nothing; stepped state is replayed from the part start (`TickStepper` resets after a backwards
seek). Feedback buffers on the GPU (p2's motion blur, p4's fire) are kept in the part (its own `Target`s, not the
pool), advanced step by step while playing forwards; after any other jump they are rebuilt by simulating the last N
steps from a cleared buffer, with N large enough that what came before contributes less than 1/1000 (p4's fire:
150 steps, notes-p4 9), or by an exact sum without state. Check: the frame at a time must be the same after seeking
forwards, after seeking backwards and while playing.

## Screen and colour

- The original's 320x200 screen is shown at 4:3 (p1's mode X too). `ctx.out` is a 4:3 RGBA16F target of any size
  (bound, cleared to black). "VGA coordinates": x 0..320, y 0..200, top-left origin; the original pixel (x, y) covers
  [x, x+1] x [y, y+1], its centre is (x + 0.5, y + 0.5); one original pixel is 1.2 times as tall as wide on screen.
  `ctx.vgaProj()`, `ctx.drawTexture(...)`, `ctx.rect(...)`, `ctx.drawPicture(...)` work in them.
- The engine parts draw into a 320-wide buffer and copy rows 1..200 of it to the screen: screen row = buffer row - 1
  (the label's buffer row 9 is screen row 8; p2's screen row 199 stays black: notes-p2 3).
- Colours are display-space floats 0..1 (no sRGB conversion). A 6-bit VGA value v is v / 63. `vgaPalette(bytes,
  first)` and `rixPalette(image, seg, off)` (`src/pictures.h`) turn DAC bytes into a `Palette` (256 `Vec3`).
- **Fades and palette mixes are colour arithmetic in display space**, so they work on redraws and on anything
  rendered in true colour:
  - the DAC scaling `(v * L) >> 8` (p6, p7 exits) is `colour * L / 256` (`fadeColour`, `fadeMul`, `PictureDraw::mul`);
  - the weight-table mixes of 0b00 (p1's A/B cross-fade, p4's background / fire mix, p5's hand-over, p6's material
    morph) are `(1 - w) A + w B` with `w = clamp(dx / 4096, 0, 1)` (`dope::weight`, `dope::mixColour`,
    `dope::mixPalettes`), mixed in display space as the DAC did;
  - p2's fade to white (2550 with step 0x100) is `min(1, colour + k / 63)` (`dope::addLevel`, `PictureDraw::add`).
- **8-bit index effects in true colour**: the original computes palette indices and the DAC turns them into colour.
  Do the colour step where the index is made, in float:
  - env maps, Phong maps and the pictures read through them (p2, p3, p5, p6): convert the map to colour through the
    part's palette first and filter the colours (indices are not linear values); where the palette changes per frame
    (p6's morph, p2's white fade) mix the two colourings or apply the fade to the result;
  - a level of a ramp (p1's 64 shades, p4's fire levels, p7's light map through its grey ramp): compute the level
    continuously and interpolate between the ramp's colours (`dpPalLerp`, `dope::PaletteTexture`), or evaluate the
    ramp's formula;
  - an index that combines layers (p4's `16 L + c`: fire level and background colour; p5's `16 hi + lo` hand-over):
    keep the layers as separate colours and mix them by the palette rule (the notes give it);
  - p2's motion blur is an exponential moving average of indices in a ramp palette: blend RGB with the same weights.
- Anti-aliased edges everywhere (MSAA for geometry, analytic coverage in shaders).

## Pictures and the art switch

`dope/remake/art/<name>.png` are faithful redraws of the hand-drawn pictures (see its README once it is written; so
far the eleven labels). The remake shows them by default; `--original-art` shows the original pixels. A part never
needs to know which:

```cpp
const Picture& neb = c.picture("p3-nebula");                    // in init(): the redraw, or the original pixels
c.drawPicture(neb);                                              // full screen (premultiplied blending, stays set)
c.drawPicture(c.picture("p7-dope"), {.y = 50, .h = 64, .mul = fadeMul(L)});   // 320 x 64 at rows 50..113, faded
c.drawPicture(label, dope::labelDraw(a));                        // the 70 x 44 box at x = a - 70, row 8, clipped
c.drawPicture(c.picture("p2-credits"), {.y = 87, .h = 32, .sy = 32.0f * (n - 1), .sh = 32});   // strip n
Indexed ix = readIndexed(*c.exe, *findPicture("p6-envmap"));     // the original's indices and palette
```

- The catalog (`src/pictures.cpp`, `pictureCatalog()`) names every picture of the notes by its dump's name in
  `dope/re/dump/pictures/` (the art's file names): `p1-logo`, `p2-credits`, `p2-label-blur`, `p2-background`,
  `p3-nebula` (also `p4-nebula`), `p3-label-phong`, `p4-label-fire-clouds`, `p4-label-fractal-scape`,
  `p4-chequerboard` (also `p5-chequerboard`), `p5-envmaps`, `p5-label-env`, `p5-label-bump`, `p5-swirl` (also
  `p6-swirl`), `p6-envmap`, `p6-label-shadow`, `p6-label-morph`, `p6-label-metal`, `p7-horizon`, `p7-dope`,
  `p7-label-light`, `p7-label-world`; and the generated maps (original pixels only): `p2-envmaps`, `p3-phongmap`,
  `p4-cloud`, `p4-landscape-height`, `p4-landscape-colour`, `p7-reflection-photo`, `p7-spot`. Each entry gives the
  part and address (`SEG:OFF` of the RIX3 header, the raw pixels or the IFF FORM), the format, the palette the
  original pixels are shown with, the frame and the kind. For anything else in a part image, pass your own
  `PictureDef` to `c.picture(def)` (a name for the cache and a possible redraw `art/<name>.png`, the part, the
  address, the format and size).
- A picture's texture covers its **frame** in original pixels (`Picture::srcW x srcH`): the whole picture (320 x 200
  screens, 70 x 44 labels, 320 x 128 credits, 320 x 64 "dope", 256 x 128 env maps), except `p1-logo` (the logo's
  1024 x 195 pixels, picture rows 1..195) and `p6-envmap` (the drawn left half, 128 x 128). A redraw has the same
  framing at its own resolution. Textures hold premultiplied alpha.
- **Kinds** (`Picture::kind`), the same for redraws and original pixels: `Screen` (colour), `Mask` (`p2-credits`:
  grey = text coverage, (index - 0x80) / 63), `Shade` (`p1-logo`: grey = shade level v / 63, coloured by the part
  through palettes A at p1 cs:04F0 and B at cs:07F8; the grey maps).
- `PictureOptions` apply to the original pixels only: `palette` (default: the catalog's), `key` (a transparent
  index), `indexAdd`. `PictureDraw`: destination rectangle in VGA coordinates, source rectangle in the frame's
  pixels, `mul` / `add` (colour = clamp(c * mul + add, 0, 1)), `alpha`, and a horizontal clip `clipX0 / clipX1`
  (the labels slide in from a screen edge). Original pixels are drawn as sharp squares with anti-aliased edges,
  redraws filtered.
- Pictures used as textures by an effect (env maps, the swirl in p5's hand-over): bind `p.tex` in your own shader,
  sample with `texSharp` (`kCommonGLSL`) if `!p.redraw`, `texture` otherwise, and remember the premultiplied alpha.
  Where the effect needs the original's indices (masks, levels, maps), use `readIndexed` and build your own textures.
- `c.art()` tells whether redraws are on; `--picshots p3-nebula,p6-envmap --shotdir DIR` (or `all`) draws pictures
  to PNG for checking.

## The core library (`src/dope/core.h`)

```cpp
#include "dope/core.h"
using namespace dope;

Integrator z{8192000, -2600};                  // p3's camera z: value, velocity per retrace before the first call
z.velocity(2100, -1100).velocity(2240, -600);  // the script sets it before the call that starts the step at 2100
double cz = z.at(c.retraces(t));               // after r calls, linear between (checked: p3's state at 3604 = notes)
LabelSlide label{-1, {{1120, 1}, {3360, -1}}};  // p3's label: speed +1 from step 9, -1 from step 21
c.drawPicture(c.picture("p3-label-phong"), labelDraw(label.position(c.retraces(t) + 1)));
c.drawPicture(lbl, labelDrawP4(d));            // p4's label: x = 319 - d, rows 148..191, cut off at x = 319
double a = angleRad(a16);                      // 16-bit angle -> radians (instead of the sine table)
int16_t s = sineEntry(c.exe->part(2), kSineQuarter[2], i);   // the original's table entry (checks)
Palette p = mixPalettes(A, B, weight(pos - 500));             // 0b00's cross-fade, continuous
Fade out{c.retraceTime(3991), kRetraceHz, 256, -7, 36};       // p6's exit fade; black from step 36
PaletteTexture pt; pt.set(pal);                // + dope::kGLSL: dpPal(pal, i), dpPalLerp(pal, i) in shaders
```

`weightTable(dx)` and `scaleTable(start, step)` are the original routines 0b00 / 2550 for exact checks. The 3D
engine goes in `src/dope/engine*` (job "engine" below). Add to the core library only what several parts share;
part agents ask for additions instead of editing it.

## Layout

- `src/main.cpp` program, options, playback, `--shots` / `--video` / `--wav` / `--bench` / `--cues` / `--picshots`;
  `src/timeline.cpp` the running order; `src/demo.*` framework (`Scene`, `REGISTER_SCENE`, `Ctx`, `TickStepper`,
  `stepsAt`, `CaptureClock`, target pool, the placeholder); `src/render.*`, `src/gl.*` GL helpers (`Shader`,
  `Texture`, `Target`, `Mat4`, `Vec3`; `kCommonGLSL` is prepended to every fragment shader: hash12, rot2,
  texBicubic, texSharp, luma); `src/exe.*` + `src/rnc.cpp` DOPE.EXE and DOPE.MOD; `src/pictures.*` palettes, the
  picture catalog and the art switch; `src/player.*` playback and the clock; `src/music.*`, `src/modplay.*`,
  `src/gf1.*` the soundtrack (music agent); `src/stub/` the silent stand-in for it.
- `src/dope/` the core library shared by several parts: `core.h`; the 3D engine in `src/dope/engine*`.
- `src/scenes/p1.cpp` .. `p7.cpp`, one file per part, registering the scenes `"p1"` .. `"p7"` (the names in
  `src/timeline.cpp`); a part may use several files (`p2_blur.cpp`, ...) and a header in `src/scenes/` for code
  shared by the parts of one job (`envbump.h` for p3 and p5, ...).
- `tools/compare.py` side-by-side check against the capture; `tools/align_art.py` the redraws' alignment;
  `tools/test_music.cpp` the music checker.

Part agents do not edit core files (`src/*.cpp`, `src/*.h`, `src/dope/*`, `CMakeLists.txt`; others build on them at
the same time): work around a problem in your own files and report it. Core agents do not edit scene files.

## Building and checking (inside the toolbox container "acme303")

Use your own build directory, and build only your own part files:

```
cd dope/remake
toolbox run -c acme303 cmake -S . -B build-<you> -G Ninja -DDOPE_PARTS="p3;p5"
toolbox run -c acme303 cmake --build build-<you>
toolbox run -c acme303 ./build-<you>/dope-remake --shots 160,191.5 --shotdir <scratch> --shotsize 960x720
toolbox run -c acme303 python3 tools/compare.py <scratch>/cmp.jpg --shots <scratch> 160 191.5 [--crop x,y,w,h] [--score]
```

Then look at `cmp.jpg` with the Read tool: left the capture's record that matches the remake at that time (from
`<scratch>/shots.txt`, which `--shots` writes; labelled with the record, its program time, the music position and
the part), right the remake. `DOPE_PARTS` lists basenames in `src/scenes`; parts that are not built show a
placeholder (name, music position, seconds and retraces since the part start, the capture record). The program finds
`dope/original/DOPE.EXE`, `DOPE.MOD` and `dope/remake/art` by itself. `--original-art` compares against the original
pixels (closest to the capture). `--video out.mp4 --from T0 --to T1` renders a range at 60 fps through the host
ffmpeg (with the music); `--bench T0,T1 --shotsize 2560x1920` reports the GPU time per frame; `--window --start T
--info` plays from T with the time, position, part and retraces in the title. `-DDOPE_SILENT_MUSIC=ON` builds
without the soundtrack (faster start-up). Other people build in this tree at the same time: if the build breaks in a
file that is not yours, wait a minute and retry; never edit someone else's file to fix it — report it in your final
message instead. Delete your build directory when you are done.

The capture (`dope/re/full.frames`, see `dope/re/README.md` and `BRIEF.md`: per displayed frame at 70.0863 Hz the
visible VGA area and the 6-bit DAC; `full.ticks` gives each record's program time, music position, part, CRTC
start, drawn-frame count and mode) shows the original as a 100 M instructions per second machine draws it: compare
composition, motion, colours and timing, not single pixels. The snapshots in `dope/re/snaps/` and the models in
`dope/re/models/` give exact intermediate values (tables, buffers, per-frame state).

## Porting jobs

| job | files | scenes / scope | notes | depends on |
|---|---|---|---|---|
| engine | `src/dope/engine.h`, `src/dope/engine*.cpp` | the shared 3D engine of p2, p3, p5, p6, p7 on the GPU: the mesh format read from the part images (vertices, faces, vertex normals, per-vertex map coordinates), object records with the camera / group slot whose position and angles are added to every object's, the float transform (`R = Rz(-c) Rx(-a) Ry(b)`, pivot, per-object projection factor f, the 0.75 vertical scale, centre (160, 99) on screen), back-face culling with the per-object sign, a depth buffer + MSAA instead of the radix-sorted painter's order (keep the original order where the notes say it is visible), the env lookup by the view-space normal (`64 + n / 512` texels of a 128 x 128 map, p6 negated), the texture + env ("bump") filler with the per-part differences of notes-engine 12 (texel source: mesh map coordinates or the object-space normal with p3's 15-bit wrap; texel shift 8 / 9 / 10; offset table static or rebuilt per retrace; the low-resolution row sampling to keep), the env-only filler, colours computed in float from the maps through the part's palette (see "Screen and colour"), and the frame composition the parts share (background picture restored behind the objects, label into the buffer, buffer row 1 = screen row 0). An API the parts call with their own per-retrace object states; a checker against the bit-exact Python model's frames (`dope/re/models/engine/`) | `notes-engine.md`; how each part uses it: notes-p2 7, notes-p3p5 2, notes-p6p7 2 | core |
| p1 | `src/scenes/p1.cpp` | `p1`: the "Complex Media Labs" logo (`p1-logo`, a shade map) scrolling right to left at half a pixel per retrace from x = 320, rows 0..193 (rows 194..199 black: the split), coloured per pixel by the A/B ramps cross-faded with `w = clamp((2 r - 500) / 4096, 0, 1)`. Small and independent of the engine: could go with p2 (or be done while the engine is built) | notes-core 4, 8 | core |
| p2 | `src/scenes/p2.cpp` (+ own files) | `p2`: the two discs and the "C" with their bump / env maps (engine), the camera script with the measured step starts (9.2), the credits band (`p2-credits` mask, its per-frame fade-in / cross-fade / fade-out as continuous decays), the motion blur (RGB exponential average with the script's weight s, rows 0..198; row 199 black), the label in the blur with its black trail, the backdrop `p2-background` from step 25, the fade to white, the white exit (retraces >= 7310) | `notes-p2.md`, notes-engine 8.1 | engine |
| p3+p5 | `src/scenes/p3.cpp`, `p5.cpp` (+ a shared header) | `p3`: the lumpy object with the Phong + bump filler (Phong map computed or `p3-phongmap` through the object palette, bump by the object-space normal with the 15-bit wrap), `p3-nebula`, the label, p3's set-up screen (colour 0 = (0, 0, 3)) from `c.load`; `p5`: the torus with the gold env map (`p5-envmaps` left) and the red bump map with the pulsing depth (right), `p4-chequerboard` behind it (p4 leaves its DAC one step darker: decide and say so), the labels, the hand-over cross-fade to `p5-swirl` at hardware speed (table above) | `notes-p3p5.md` | engine |
| p4 | `src/scenes/p4.cpp` (+ own files) | `p4`: the fire feedback through the polar swirl map (`p4-cloud`, the float recursion or the exact sum, 35.04 steps per second on odd IRQs), the fire level instead of the dither, the background / fire mix with the fade A over `p3-nebula`, the voxel landscape (`p4-landscape-height`, `p4-landscape-colour`, ray-marched per pixel), the change to `p4-chequerboard` and its fade-in, the labels at the bottom right (`labelDrawP4`) | `notes-p4.md` | core |
| p6+p7 | `src/scenes/p6.cpp`, `p7.cpp` | `p6`: the cat and the rings over `p5-swirl` (p6 sets no mode), the shadow masks, the fur bump with its strength, the material morph (palette mix per retrace), the labels, the exit fade at hardware speed; `p7`: the tentacle object with the light map (`p7-reflection-photo` + five `p7-spot`s per frame along their paths) through the grey ramp, `p7-horizon`, the labels, "dope" (`p7-dope`) for 350 retraces and the exit fade at hardware speed | `notes-p6p7.md` | engine |

The engine, p1 and p4 can start now; p2, p3+p5 and p6+p7 start when the engine's API is in place (they can write
their 2D parts — backgrounds, labels, fades, hand-overs — first).

## Quality

- Smooth at 60+ fps at 2560x1920 on a mid-range GPU; all initialisation of all parts together under 2 s (the exe
  and the pictures take about 0.1 s now, the soundtrack about 0.3 s).
- Anti-aliased edges (analytic coverage in shaders, or MSAA for geometry).
- Portable C++20 (g++, MinGW, MSVC), no new dependencies. Every file starts with a comment saying what it draws or
  provides and which original routines (by address) it ports. Small readable functions.
- Write down in `dope/remake/README.md` ("Known differences") what cannot be exact, and in the scene file what was
  evaluated exactly instead of the original's shortcut.

## Your final message

Under 300 words: what you built (files, public API), how you verified it (compare sheets, numbers), what differs
from the original and why, and what is left.
