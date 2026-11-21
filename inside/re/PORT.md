# Inside remake — shared brief for everyone writing code in `inside/remake`

*Inside* (CNCD, The Gathering 1996 PC demo; code Wili, Jouni and Juliet, graphics Destop, additional graphics Santtu,
music Yolk and Legend; the SurRender 3D engine by Hybrid, DSMI by Otto Chrons) is remade in C++20 / SDL3 / OpenGL
3.3 core. The goal: **the same as the original, but much sharper**. Same motion, timing, composition, colours and
music as the original, rendered at any resolution in true colour.

The original has been reverse engineered. Read the notes before writing code:

- `inside/re/NOTES.md` (unpacking, INSIDE.DAT, the clock, the script, the palette system, the screen),
  `PARTS.md` (the timeline with refreshes, music positions and script entries);
- `inside/re/notes-*.md`: `core`, `music`, `engine` (SurRender), and one per part: `intro-logo`, `credits`,
  `sprites`, `flower`, `flight`, `water`, `twirl`, `temple`. Every routine is pseudo-C with exact constants, checked
  against the emulator; the Python models in `inside/re/models/<scope>/` give exact reference values;
- `AGENTS.md` in the repository root (the rules).

## Fidelity rules

- Keep: timing, motion paths, camera, composition, colours, random sequences (replay Watcom's rand() from the seed
  the notes give), and the visible quirks that are part of the look (each notes file lists them and says which ones a
  remake may drop).
- Evaluate exactly instead of copying low-resolution shortcuts: per-pixel tables (the twirl's polar table) become
  formulas; affine texture mapping becomes perspective-correct; nearest-neighbour texel reads become filtered reads
  **in colour** (look each tap's index up, then blend the colours: never filter indices); 64 shade levels become a
  continuous level (blend the shade table's two neighbouring rows in colour); integer positions and truncated
  normals become floats; the painter's order becomes a depth buffer where nothing visible depends on the order (the
  notes say where it does, e.g. the flower's sort bias); polygon edges are anti-aliased (MSAA or analytic coverage).
- Drop artefacts that only exist because of 320 × 200 / 8 bit (truncation steps, 6-bit palette steps, aliasing,
  seams of the fixed point, stray pixels past a table's end, the first frame of a part shown through the next part's
  palette), but never change what a viewer would describe as the content.
- Nothing from the originals may be copied into the source: read every table, picture, palette, mesh and parameter
  at run time: `c.exe->u8/u16/s16/u32/s32/f32/f64/cstr/bytes/span(addr)` (`src/exe.h`, linear addresses as in the
  notes: object 1 at 0x400000, object 2 at 0x420000; BSS addresses hold nothing) and `c.dat->file(name)` for
  INSIDE.DAT. **Read the file names from the exe too** (the strings the routines pass to 0x400614, by address), so
  the source holds addresses rather than the demo's strings. Constants that a routine stores as immediates in its
  code (parameter tables built in an init) are read with `inside::traceStores(exe, begin, end)` (`src/inside/code.h`,
  see `scenes/twirl.cpp`). Code constants (multipliers, loop counts, addresses, offsets, format constants) may appear
  in the source, and so may values measured from the capture.

## Time

**The clock is the screen refresh of the original.** `render(c, r)` gets `r` = a fractional refresh number counted
as in the capture: refresh k is record k of `inside/re/full.frames`, program time k / 70.086 s. The music starts at
refresh 37 (`Soundtrack::startRefresh`); playback, `--start`, `--shots` and `--video` work in seconds of music time
and convert (`music.refreshOf(t)`); `--refshots R1,R2` renders refreshes directly, for comparing with the capture.

The master clock of the original is **M** = [0x4248c4], its timer callbacks since the music start, 4 per refresh
(notes-core.md section 1, which every port should read): record k of the capture follows the retrace callback
**M = 4k − 150**; the music player runs on DSMI time 35 M; the parts' tick routines and the palette steps run at the
retrace callbacks; the script runs an entry when its row has started.

| clock | in the remake |
|---|---|
| tick steps whose result record r shows (the tick routine runs at the retrace callbacks of records start, start + 1, ...; the frame shown at r was drawn after the one of r − 1) | `c.refreshes(r)` = r − `c.start`: evaluate the stepped state continuously at n = r − c.start |
| M at the retrace callback of record r | `Ctx::callback(r)` = 4 r − 150 (`Ctx::refreshOfCallback(m)` the other way) |
| what a **tick routine** reads: M and P at its call at record k | M = callback(k), P = `c.partTicks(M)` = M − `c.cue->mReset` (the callback in which the part's routines were set) |
| what a **frame routine** reads for the frame shown at record r | the parts that wait for the copy (sprites, flower) draw right after the retrace callback of r − 1: M = callback(r) − 4; the others draw as fast as the machine allows (the capture: 4..10 callbacks before the copy, a fast machine 0..1): use M = callback(r) − 1 (notes-core 1.5, 1.7) |
| the music position order · 64 + row as the script sees it | `c.position(r)`; `c.music->rowCallback(row)` = the callback in which song row R starts |
| the script table 0x4222e4 (read from the exe) | `c.script()`: each entry's callback (its row's start, never before the entry before it) and refresh (the first record that shows its effect); `c.entryRefresh(routine, n)` (the n-th entry calling that routine), `c.entryCount(routine, r)`, `c.lastEntry(routine, r)` |
| the capture | `c.captureRecord(r)` = the `full.frames` record shown at r |

**Part starts** (`src/timeline.cpp`, `inside-remake --cues`): a cue runs from the first record that shows the script
entry that sets the part's routines to the next part's. The original runs an entry at the end of the first frame
drawn after its row started, 0..8 callbacks late depending on the machine (notes-core 1.4); the remake runs it in the
callback in which the row starts, as an infinitely fast machine would, so a part or an event may start one record
earlier than in the capture.

**Events inside a part** (script entries that call the part's event routines: the twirl's 0x40a098, the flower's
0x409358 / 0x4093f0, the credits' 0x405f1c ...): apply them at `e.refresh` from `c.script()`. Their state changes
take effect from that refresh; a step that the original makes "at the next tick" is the step from n to n + 1 after
it (see `scenes/twirl.cpp`).

**Stepped state.** Everything the parts animate is stepped per refresh in the tick routines (none of the parts read
a timer or the frame rate). Replay it from the part start on every frame with closed forms or `TickStepper`, and
blend continuous quantities between step n and n + 1 by the fraction, so motion is smooth at any refresh rate. Float32
sums in the original (0.15 per tick and similar) may be replayed as float32 sums where a wrap or threshold depends on
them (the notes say where). Sudden changes stay on their step.

**Seeking.** `render(c, r)` must depend only on `r`: players seek, and shots and videos render refreshes in any
order. Check: the frame at a refresh must be the same after seeking forwards, after seeking backwards and while
playing.

## Screen and colour

- The original's 320 × 200 8-bit screen is shown at 4:3. `c.out` is a 4:3 RGBA16F target of any size (bound, cleared
  to black). "VGA coordinates": x 0..320, y 0..200, top-left origin; the original pixel (x, y) covers [x, x+1] ×
  [y, y+1], its centre is (x + 0.5, y + 0.5); one original pixel is 1.2 times as tall as wide on screen.
  `c.vgaProj()`, `c.drawTexture(...)`, `c.rect(...)` work in them.
- **Colour = the palette entry of the pixel's index**, as a display-space float: a 6-bit value v is v / 63 (no sRGB
  conversion). Draw with the part's base palette (the one it passes to 0x400364).
- **The palette fades and flashes are not the part's job.** The script's 0x400498 (white flash), 0x400454 (inverse
  flash), 0x400438 (fade to black) and the flashes inside part routines change what the whole screen shows; the
  framework applies them after the part has drawn, as out = colour · a + b (`src/inside/fade.*`). If your part's
  routines call 0x400498 / 0x400454 / 0x4004ac / 0x400364 with a fade, tell the coordinator (the list in fade.cpp).
  A part that writes the palette itself (cycling, its own fades) does that in its colours.
- `src/inside/indexed.h`: `inside::uploadIndices(span, w, h)` (GL_R8UI, read with `texelFetch` from a
  `usampler2D`), `inside::uploadPalette(colours)` (256 × 1), `inside::palette(dat, file, black0)`, GLSL
  `kIndexedGLSL` (`palColor(pal, index)`, `idxAt(tex, texel)` with wrapping). Filter in colour: fetch the four texels
  around the sample point (texel n covers [n, n+1), so sample at uv − 0.5), look each up, blend; the twirl does it
  with its own address rule.
- **Shade tables** (`.shd`, 64 or 256 levels × 256 colours: index = shd[level · 256 + colour]): upload them as an
  R8UI texture (256 × levels) and look up both neighbouring levels of the continuous light level, then blend the two
  colours. Blend tables (`.int`, `.qnt`, `.sdw`, `.fog`): see the notes of your part for what they compute; evaluate
  that in colour.

## Pictures

`inside/remake/art/<name>.png` will hold the redraws of the pictures drawn as they are (a separate job; the notes say
which pictures qualify: photos of real people are never redrawn, and anything read through palette indices or shade
tables stays the original's). `c.picture(name)` gives a `Picture` (`src/pictures.h`: the original's pixels through
their palette, or the redraw), `c.drawPicture(p, PictureDraw{...})` draws it. Register a picture you draw as it is in
`pictureList()` (`src/pictures.cpp`) through the coordinator.

## Layout

- `src/main.cpp` program and options; `src/timeline.cpp` the running order; `src/demo.*` framework (`Scene`,
  `REGISTER_SCENE`, `Ctx`, `TickStepper`, the target pool, the placeholder, the fade pass); `src/render.*`, `src/gl.*`
  GL helpers (`Shader`, `Texture`, `Target`, `Mat4`, `Vec3`; `kCommonGLSL` is prepended to every fragment shader);
  `src/exe.*` + `src/unpack.cpp` INSIDE.EXE and INSIDE.DAT; `src/pictures.*` the pictures and the art switch;
  `src/player.*` playback; `src/music.*`, `src/gf1.*` (and the player port) the soundtrack.
- `src/inside/` the core library shared by the parts: `fade.*`, `indexed.*`, `code.*`, and the 3D engine.
- `src/scenes/<scene>.cpp` one file per part, named as the scenes of timeline.cpp (`intro`, `logo`, `credits`,
  `sprites`, `flower`, `flight`, `water`, `twirl`, `temple`), each with `REGISTER_SCENE(MyScene, "<scene>")`.
- `tools/compare.py` side-by-side check against the capture.

Part agents do not edit core files (`src/*.cpp`, `src/*.h`, `src/inside/*`, `CMakeLists.txt`; others build on them
at the same time): work around a problem in your own files and report it.

## Building and checking (inside the toolbox container "acme303")

Use your own build directory, and build only your own part files:

```
cd inside/remake
toolbox run -c acme303 cmake -S . -B build-<you> -G Ninja -DINSIDE_PARTS="flower"
toolbox run -c acme303 cmake --build build-<you>
toolbox run -c acme303 ./build-<you>/inside-remake --refshots 5000,5634.5 --shotdir <scratch> --shotsize 960x720
toolbox run -c acme303 python3 tools/compare.py <scratch>/cmp.jpg --shots <scratch> 5000 5634.5 [--crop x,y,w,h] [--score]
```

Then look at `cmp.jpg` with the Read tool: left the capture's record, right the remake at that refresh.
`INSIDE_PARTS` lists basenames in `src/scenes`; parts that are not built show a placeholder. `--nofade` leaves out
the palette fades (compare the part's own colours). `--video out.mp4 --from T0 --to T1` renders a range in seconds
of music time at 60 fps; `--bench T0,T1 --shotsize 2560x1920` reports the GPU time per frame; `--window --start T
--info` plays from T with the position, part and refresh in the title. Delete your build directory when you are done.

The capture shows the original at 150 M instructions per second: compare composition, motion, colours and timing,
not single pixels. The snapshots in `inside/re/snaps/` and the models in `inside/re/models/` give exact intermediate
values.

## Quality

- Smooth at 60+ fps at 2560 × 1920 on a mid-range GPU; all initialisation of all parts together under 2 s.
- Anti-aliased edges (MSAA for geometry, or analytic coverage in shaders).
- Portable C++20 (g++, MinGW, MSVC), no new dependencies, no warnings. Every file starts with a comment saying what
  it draws or provides and which original routines (by address) it ports. Small readable functions.
- Write down in your final message what cannot be exact (it goes into `inside/remake/README.md`, "Known
  differences"), and in the scene file what was evaluated exactly instead of the original's shortcut.

## Your final message

Under 300 words: what you built (files, public API), how you verified it (compare sheets, numbers), what differs
from the original and why, and what is left.
