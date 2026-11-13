# clone meets clone remake — shared brief for everyone writing code in `clone/remake`

*clone meets clone* (Acme & Fudge, Takeover '98 64k intro, MS-DOS; design Aap and Echo, effects Echo, music Simon,
graphics Aap; texture mapper and grid filler Craft, radial blur originally Dune) is remade in C++20 / SDL3 / OpenGL
3.3 core. The goal: **the same as the original, but much sharper**. Same motion, timing, composition, colours and
music as the original, rendered at any resolution in true colour, with faithful high-resolution redraws of Aap's
pictures (or the original pixels with `--original-art`).

The original has been reverse engineered. Read the notes before writing code:

- `clone/re/NOTES.md` (file layout, the VE container, MXMPlay and the timer, main's flow, the sync table),
  `PARTS.md` (the part timeline with ticks, positions, frame rates and what depends on the frame rate);
- `clone/re/notes-*.md`: `core` (main, **t70 and dt exactly (section 5)**, the event and part switches, the random
  generator, the 2D helpers, corrections to NOTES.md in section 18), `gfx` (the textures and the ten pictures,
  where and how each is drawn), `music`, `engine` (the 3D engine and Craft's texture mapper; section 10 is the colour
  of a pixel as a closed form), `parts3d` (parts 7..10), `grid` (Craft's grid filler, parts 11, 2, 5, 4; section 3
  "the per-pixel function"), `particles` (parts 1, 3, 6). Every routine is pseudo-C with exact constants, checked
  against the emulator; the Python models in `clone/re/models/<scope>/` give exact reference values;
- `AGENTS.md` in the repository root (the rules), `clone/remake/art/README.md` (the redraws, when it exists).

## Fidelity rules

- Keep: timing, motion paths, camera, composition, colours, random sequences (replay the generator 0x420491 from its
  seed, `cmc::Random`), and the visible quirks that are part of the look (each notes file lists them and says which
  ones a remake may drop).
- Evaluate exactly instead of copying low-resolution shortcuts: 8 × 8 block interpolation (the grid filler, the
  8-bit expansions) becomes per pixel; perspective every 8 pixels becomes per pixel; the log / exp tables, the
  multiply table and the 16 × 16 weight tables become float maths; 8.8 / 16.16 fixed point becomes float; truncated
  integer levels become continuous; nearest-neighbour reads become filtered reads; the painter's sort becomes a
  depth buffer where nothing visible depends on it; polygon edges are anti-aliased (MSAA or analytic coverage); 4 × 4
  stamps become sprites of the same footprint; repeated box blurs become Gaussians of the same variance.
- Drop artefacts that only exist because of 320 × 200 / 8 bit (truncation steps, aliasing, seams, block noise), but
  never change what a viewer would describe as the content.
- Nothing from CLONE.EXE may be copied into the source: read every table, picture, palette, mesh, constant table and
  the module at run time through `CloneExe` (`src/exe.h`): `c.exe->u8/u16/s16/u32/s32/f32/f64/bytes/span(addr)` at
  the notes' linear addresses (the image is relocated for 0x400000, as in the notes; BSS addresses hold nothing:
  the program computes them). Constants that are code (multipliers, loop counts, immediates, addresses, offsets,
  format constants) may appear in the source, and so may values measured from the capture.

## Time

**The clock is music time.** `render(ctx, t)` gets `t` = xmpGetTimer / 1,193,182 s: t = 0 is the player's second
timer interrupt, when row 00:00 starts to sound (the end of tick 12 of `full.frames`, program time 0.7216 s). The
soundtrack (`src/music.h`, pcm frame 0 = t 0) uses the same zero. The intro ends at 216.910 s (main stops the music
after interrupt 11713, three ticks after the row 3c:3f of event −1 was read).

The original's clocks, all in `Ctx` (`src/demo.h`):

| clock | in the remake |
|---|---|
| xmpGetTimer (PIT clocks since the music start, interpolated inside the tick) | `c.timer(t)` = t · 1,193,182 |
| main's t70 (double [0xe9572e]) = timer / **17043** (0x41a128; 70.0101 per second, not 70) | `c.t70(t)`, `c.t70ToTime(u)` |
| the part's integrated dt: what every animated variable of a part is a function of | `c.clock(t)` = clock0 + t70(t) − t70(origin), in t70 units |
| xmpGetPos as main uses it: order · 64 + row, **one player tick ahead of the sound** | `c.position(t)`; `c.positionTime(p)` = when main sees row p (events fire); `c.rowTime(p)` = when it is heard (one tick, 18.52 ms, later) |
| the sync table 0x420499 (98 entries, read from the exe) | `c.syncTable()`, `c.eventTime(ev, n)` (the n-th entry with that event), `c.eventCount(ev, t)`, `c.lastEvent(ev, t)` |
| frames drawn by the original | `c.frames(t)` = (t − origin) · the capture's frame rate in the part (`Cue::fps`, PARTS.md) |
| the capture | `c.captureTick(t)`: the `full.frames` record nearest to t |

(`Soundtrack::positionAt` returns order << 8 | row, the player's own encoding; the framework uses order · 64 + row as
main and the sync table do. Both agree on the times.)

**No part reads the timer.** main computes t70 after every shown frame and dt = t70 − previous t70; each part
advances its state by dt once per drawn frame. So a variable `v += k · dt` from v0 at the part's first frame is
`v0 + k · c.clock(t)` (notes-core 5.4, every notes file gives the recurrences and their closed forms). A part's
clock starts at the timer of the frame before its first frame, the previous part's last frame, 0..17 ms before its
event (`origin`, the capture's, notes-core 5.3). Part 1 starts with the image's dt = 6.0 (clock0 = 6.0), and its
second run (event 111, no set-up) continues its clock: clock0 = 6.0 + 2114.1087.

**Part starts** (`src/timeline.cpp`: one cue per sync event that selects a part; `clone-remake --cues`):

| scene | part | event | routine | start (s) | end (s) | origin (s) | clock0 | capture fps | job |
|---|---|---|---|---|---|---|---|---|---|
| `swarm` | 1 | 1 | 0x405527 | −0.0185 | 30.2092 | 0 | 6.0 | 45.8 | particles |
| `tiles` | 11 | 11 | 0x419dc7 | 30.2092 | 42.6559 | 30.1972 | 0 | 725 | grid |
| `tube` | 2 | 2 | 0x4198bf | 42.6559 | 56.8808 | 42.6557 | 0 | 40.5 | grid |
| `blob` | 8 | 8 | 0x417929 | 56.8808 | 72.4392 | 56.8655 | 0 | 54.0 | parts3d |
| `ring` | 6 | 6 | 0x406677 | 72.4392 | 85.3304 | 72.4326 | 0 | 28.9 | particles |
| `cubes` | 9 | 9 | 0x4185e5 | 85.3304 | 113.7800 | 85.3299 | 0 | 54.6 | parts3d |
| `zoom` | 5 | 5 | 0x403530 | 113.7800 | 128.0049 | 113.7635 | 0 | 41.6 | grid |
| `bumps` | 7 | 7 | 0x416fce | 128.0049 | 145.7859 | 127.9878 | 0 | 72.2 | parts3d |
| `dots` | 3 | 3 | 0x405c45 | 145.7859 | 172.4574 | 145.7819 | 0 | 51.9 | particles |
| `tunnels` | 4 | 4 | 0x4037c0 | 172.4574 | 192.0166 | 172.4562 | 0 | 39.0 | grid |
| `slabs` | 10 | 10 | 0x41958e | 192.0166 | 206.2414 | 192.0013 | 0 | 83.1 | parts3d |
| `swarm` | 1 | 111 | 0x405527 | 206.2414 | 216.9100 | 206.2394 | 2120.1087 | 26.6 | particles |

`ctx.cue`, `c.start`, `c.end`, `c.origin`, `c.clock0`, `c.part` describe the cue being drawn. The original changes
part at the first frame after the event (PARTS.md has the capture's frames); the remake at the event's time. If a
start in your notes differs from this table, report it (timeline.cpp is a core file).

**Events inside a part.** Events run once per frame before the part draws, so the original applies an event's new
rule from the timer of the frame before the event frame (τ in notes-grid 8 and notes-parts3d 2.8: 0..1 frame before
the row). The remake applies it at `c.eventTime(ev, n)` (the original at an infinite frame rate). Where the capture's
lag matters for the look (part 2's camera, part 5's direction flips, part 9's axis switches), the notes give the
capture's τ: use them and say so in the scene file.

**Per-frame state.** What the original advances once per frame without dt, or nonlinearly in dt (part 1's emitter,
targets and feedback, part 3's magnify-and-decay, part 6's spawns and target, the truncating `(int)(v + k dt)`
targets, part 3's caption level), depends on the frame rate. Step it exactly as the original at the capture's frame
steps (notes-particles 7 gives dt per frame in each phase; PARTS.md the rates) with `TickStepper`
(`steps = c.frames(t)` or your own rate function) and blend continuous quantities between step k and k + 1 by the
fraction, so motion is smooth at any refresh rate. Sudden changes stay on their step.

**Seeking.** `render(ctx, t)` must depend only on `t`: players seek, and shots and videos render times in any order.
Cheap stepped state is replayed from the part start on every frame (`TickStepper` resets after a backwards seek).
Feedback buffers on the GPU are kept in the part (its own `Target`s, not the pool), advanced step by step while
playing forwards; after any other jump rebuilt by simulating the last N steps from the cleared buffer, with N large
enough that what came before contributes less than 1/1000 (15/16 per step: 107 steps), or from the part start if
the state never forgets. Check: the frame at a time must be the same after seeking forwards, after seeking backwards
and while playing.

## Screen and colour

- The original's 320 × 200 true-colour screen (0x00RRGGBB) is shown at 4:3. `ctx.out` is a 4:3 RGBA16F target of
  any size (bound, cleared to black). "VGA coordinates": x 0..320, y 0..200, top-left origin; the original pixel
  (x, y) covers [x, x+1] × [y, y+1], its centre is (x + 0.5, y + 0.5); one original pixel is 1.2 times as tall as
  wide on screen. `ctx.vgaProj()`, `ctx.drawTexture(...)`, `ctx.rect(...)` (0x4043be), `ctx.overlay(...)` work in
  them.
- Colours are display-space floats 0..1 (no sRGB conversion): an 8-bit channel v is v / 255, a 6-bit grey v / 63.
- **Rows a part does not write** show, in the original's 32-bit mode, its picture of four frames earlier (four
  video pages), in the capture always black (notes-parts3d 2.5, notes-particles 9): **black** in the remake. The
  frame is cleared before `render()`.
- **Log-encoded colour**: the textures store brightness as 8-bit levels L = 128 + K ln(value) and the fillers add a
  shade to L and decode with an exp table, i.e. they multiply the linear value by a gain and clamp at 255 (the clamp
  to white is part of the look). `cmc::gain23(k)` (grid filler, 4.4 % per level) and `cmc::gain90(k)` (texture
  mapper, 1.1 % per level), GLSL `cmDecodeMap` / `cmDecodeTable` (`textures.h`). notes-engine 10 and notes-grid 3
  give the closed forms of a pixel's colour.
- **8-bit buffers** (parts 1, 3, 6) are expanded through palettes per 8 × 8 block (0x404a08): keep float intensity
  buffers and colour them per pixel (notes-particles 8: colour = min(255, C · v / 92)); `cmc::PaletteTexture` and
  GLSL `cmPal` / `cmPalLerp` if you need a palette lookup.

## Pictures and overlays

`clone/remake/art/<name>.png` are faithful redraws of the ten stored pictures (another agent is making them; names
as in notes-gfx 7.1): `meet_us`, `get_them_clones`, `connect_the_dots`, `tunnels_meet_more_tunnels`, `glyph1`..
`glyph4`, `title_clone_meets_clone`, `credits_aap_echo_simon`. The remake shows them by default; `--original-art`
shows the original pixels (palettes A, B, C from the exe; transparent index 0, and 0xf0 for the captions). A part
never needs to know which:

```cpp
// part 2's caption (notes-gfx 7.2): black box, text, glyph, after the part's own picture
c.rect(10, 130, 100, 40, Vec3(0, 0, 0));           // 0x4043be
c.overlay("meet_us", 20, 151);                     // 0x404002 mode 0: opaque
c.overlay("glyph1", 80, 111);
// part 3: the blend 0x40442c at a level 0 (opaque) .. 63 (1/64 left), continuous (no truncation)
c.overlay("connect_the_dots", 212, 160, level);
// part 11's title and part 1's title / credits with their fades (closed forms in clone/core.h)
c.overlay("title_clone_meets_clone", 141, 104, cmc::tilesTitleLevel(c, t));
```

- `c.overlay(picture or name, x, y, level)`: top-left at (x, y) in VGA coordinates, 1:1 in original pixels; opacity
  `overlayOpacity(level)` = 1 − clamp(level, 0, 63) / 64, premultiplied blending (`Blend::Premul` stays set).
- `c.picture(name)` gives the `Picture` (texture covering exactly the original's w × h pixels, premultiplied alpha,
  `redraw` flag); `c.drawPicture(p, PictureDraw{...})` draws it anywhere (source and destination rectangles,
  `mul` / `add`, `alpha`). Original pixels are drawn as sharp squares with anti-aliased edges, redraws filtered.
- Text redraws are white letters whose grey is their coverage (no black around them): over the black boxes they look
  like the original; over part 1's particles they have no dark rim (notes-gfx 7.3: deliberate).
- `--picshots all --shotdir DIR` draws the ten pictures (redraw or original pixels) to PNG for checking; `--picshots
  mapA,mapB,mapC,table1,table2` the generated textures.
- The placeholders of parts that are not built draw their boxes, captions and glyphs (opaque), so the compare
  sheets show the overlays at their places from the start.

## The core library (`src/clone/`, namespace `cmc`: "clone" is a POSIX function)

- `clone/core.h`: `Random` (0x420491; `skip(n)` in O(log n); `kRandomAtPart6` 84,546, `kRandomAtPart3` 216,354,
  `kRandomAtPart1Again` 315,145: the numbers drawn before those parts in the capture, which give the seeds of
  notes-particles 3.1 exactly); `Ramp` (a value advanced `clamp(v + rate · dt)` per frame from a time: closed form
  of t70); `titleLevel`, `creditsLevel`, `tilesTitleLevel` (the overlay fades of notes-gfx 7.4); `gain23`, `gain90`,
  `enc23`, `enc90` (0x4034e7, 0x4167b7); `tileColour(exe, idx)` (0x428164, the mosaic of parts 7..11, value · 0.0125
  per channel); `PaletteTexture`; `kFeedbackWeight` (0x4045b4); GLSL `kGLSL`.
- `clone/textures.h`: the five generated textures (maps A, B, C; the grid's tables 1 and 2), exact formulas of
  notes-gfx 4: `c.shared<cmc::Textures>().get(Tex::MapB, style, form)`, `grid(c, t)` (table 1 for part 2, table 2
  from event 5: parts 5 and 4). Style `Smooth` (default: continuous radius, 1024² per period, mipmapped,
  anisotropic) or `Texel` (the original's 256² texels with the integer radius, evaluated in float: the look closest
  to the capture); form `Linear` (V / 255, V = 1 + x) or `Log` (the level L / 255 without truncation, to filter in
  the log domain as the fillers do and decode after the filter: notes-engine 10 asks for this). Sample original
  texel (u, v) at s = (u + 0.5) / 256 (texel centres at integer coordinates); the grid filler's (u, v) are (row,
  column) of the texture. `texValue` / `texLevel` evaluate them on the CPU; GLSL `kTexGLSL` (`cmTexValue`,
  `cmDecodeMap`, `cmDecodeTable`). Checked: the texel style is +0.6..1.1 levels against the original bytes, as
  notes-gfx 4 predicts for "no truncation".
- `src/clone/engine*` (engine job, below): the 3D engine.

## Layout

- `src/main.cpp` program, options, playback, `--shots` / `--video` / `--wav` / `--bench` / `--cues` / `--picshots`;
  `src/timeline.cpp` the running order; `src/demo.*` framework (`Scene`, `REGISTER_SCENE`, `Ctx`, `TickStepper`,
  `stepsAt`, target pool, the placeholder); `src/render.*`, `src/gl.*` GL helpers (`Shader`, `Texture`, `Target`,
  `Mat4`, `Vec3`; `kCommonGLSL` is prepended to every fragment shader: hash12, rot2, texBicubic, texSharp);
  `src/exe.*` + `src/unpack.cpp` CLONE.EXE (the VE container: LZ77 + canonical Huffman and the fixups, checked
  against the release by hash, 1 ms); `src/pictures.*` the ten pictures (the art switch); `src/player.*` playback
  and the clock; `src/music.*`, `src/mxmplay.*`, `src/gf1.*`, `src/modsynth.*` the soundtrack (music agent).
- `src/clone/` the core library shared by several parts (above).
- `src/scenes/<scene>.cpp` one file per part, named as the scenes in the table above, each with
  `REGISTER_SCENE(MyScene, "<scene>")`. Code shared by the parts of one job goes in a header in `src/scenes/` (e.g.
  `grid.h` for Craft's filler, `particles.h` for the stamps, the radial blur and the feedback, `tiles3d.h` for the
  tile band of parts 7..10), or a `.cpp` there that you list with your parts in `CLONE_PARTS`.
- `tools/compare.py` side-by-side check against the capture; `tools/align_art.py` the redraws' alignment;
  `tools/test_music.cpp` the music checker.

Part agents do not edit core files (`src/*.cpp`, `src/*.h`, `src/clone/core.*`, `src/clone/textures.*`,
`CMakeLists.txt`; others build on them at the same time): work around a problem in your own files and report it.
The engine agent owns `src/clone/engine*`. Core agents do not edit scene files.

## Building and checking (inside the toolbox container "acme303")

Use your own build directory, and build only your own part files:

```
cd clone/remake
toolbox run -c acme303 cmake -S . -B build-<you> -G Ninja -DCLONE_PARTS="tube;zoom"
toolbox run -c acme303 cmake --build build-<you>
toolbox run -c acme303 ./build-<you>/clone-remake --shots 45,50.5 --shotdir <scratch> --shotsize 960x720
toolbox run -c acme303 python3 tools/compare.py <scratch>/cmp.jpg --shots <scratch> 45 50.5 [--crop x,y,w,h] [--score]
```

Then look at `cmp.jpg` with the Read tool: left the capture's record whose frame was drawn with the timer closest to
T (labelled with the tick, the flip, the music time it was drawn with, order:row and main's part), right the remake.
`--tick` takes capture ticks instead of times (the shot must be at the record's drawn time, printed by compare.py).
`CLONE_PARTS` lists basenames in `src/scenes`; parts that are not built show a placeholder (name, part number,
position, the part's clock in t70 units, seconds since the start, the routine, the capture tick, and the part's
captions). The program finds `clone/original/CLONE.EXE` and `clone/remake/art` by itself. `--original-art` compares
against the original pixels (closest to the capture). `--video out.mp4 --from T0 --to T1` renders a range at 60 fps
through the host ffmpeg (with the music); `--bench T0,T1 --shotsize 2560x1920` reports the GPU time per frame;
`--window --start T --info` plays from T with the time, position, part and t70 in the title.
Other people build in this tree at the same time: if the build breaks in a file that is not yours, wait a minute and
retry; never edit someone else's file to fix it — report it in your final message instead. Delete your build
directory when you are done.

The capture (`clone/re/full.frames`, see `clone/re/README.md`: per timer tick (53.99 per second) the picture on screen
at the end of the tick; `full.ticks` and `full.flips` give each frame's timer value, position and part) shows the
original at 150 M instructions per second: compare composition, motion, colours and timing, not single pixels. The
snapshots in `clone/re/snaps/` and the models in `clone/re/models/` give exact intermediate values (tables, buffers,
per-frame state).

## Porting jobs

| job | files | scenes / scope | notes | depends on |
|---|---|---|---|---|
| engine | `src/clone/engine.h`, `src/clone/engine*.cpp`, a checker in `tools/enginetest/` | the 3D engine on the GPU as parts 7..10 use it: object records (vertices, faces with per-vertex normals or per-vertex shades, uv, map), the matrix routines 0x404c9f..0x4050e9 (Rx, Ry, Rz, scale, translate as the parts build them), vertex normals 0x416b61, the shade (normals: (1 − n_z)³ · 2437939.2 · light; per-vertex shades) times the light [0x427f70], the projection with the window of 0x414f04 (events 7 / 10: full height for part 7, rows 20..179 for 8..10) and the −0.5-pixel sample shift, Craft's mapper as a shader (perspective-correct uv, the map filtered in the log form and decoded with the shade, clamp at 255: notes-engine 10), a depth buffer + MSAA instead of the 4096 buckets, keeping the far cut (faces with mean z ≥ 6826.7 dropped: part 8's fly-in); the geosphere generator 0x4174c4 (octahedron 0x427f9c / 0x427f84, level 4), optionally at a finer level with float vertices. An API the 3D parts call with their own geometry and motion; a checker against the dumps of `models/engine/` | `notes-engine.md`; how the parts call it: `notes-parts3d.md` 2.1..2.7 | core |
| parts3d | `src/scenes/bumps.cpp` (part 7), `blob.cpp` (8), `cubes.cpp` (9), `slabs.cpp` (10), shared `src/scenes/tiles3d.h` | part 7: the deformed geosphere with map A, the tile block, fade-in, events 701; part 8: the blob with map B flying in from z = 9400, the tile band appearing by event 801, darkening by 803; part 9: three metaballs on the 6 × 6 × 6 voxel grid as cubes with map C, event 901, the axis switches of 902 (the capture's switch times, notes-parts3d 5.3); part 10: the 4 × 10 × 4 grid as slabs with map B, the light pulse 1011 / 1012. The tile band 2.4 (`cmc::tileColour`), the black rows | `notes-parts3d.md` | engine |
| grid | `src/scenes/tiles.cpp` (part 11), `tube.cpp` (2), `zoom.cpp` (5), `tunnels.cpp` (4), shared `src/scenes/grid.h` (Craft's grid filler as a per-pixel shader: u, v, L per pixel, the table texture through `cmDecodeTable`, the clamp; mipmaps where the original shows block noise) | part 11: the 8 × 6 tiles with their flicker and the title fade (event 1101); part 2: the flat tube, the camera paths and brightness (event 201; the capture's position lag Δ = 1.73 units, notes-grid 8), "meet us" and glyph 1; part 5: the warped rotating zoom, the direction flips (501), "get them clones" and glyph 2; part 4: the three tunnels, the rate modes (401, 403), "tunnels meet more tunnels" and glyph 4 | `notes-grid.md` (3 the filler and its per-pixel function, 4..7 the parts, 8 the time base); textures `notes-gfx.md` 2..4 | core |
| particles | `src/scenes/swarm.cpp` (part 1, both runs), `dots.cpp` (3), `ring.cpp` (6), shared `src/scenes/particles.h` | the 8-bit intensity buffers as float targets, the 4 × 4 stamps as sprites, Dune's radial blur (3.5), the feedback 0x4045b4 (part 1), part 3's magnify-and-blur; part 1: the swarm, its emitter and targets stepped at the capture's frame steps, events 101..107 and 111 (blur modes, the title fade from 106, the flash 105, the credits 107: `cmc::titleLevel` / `creditsLevel`), the second run continuing the first; part 6: the blue ring and its bursts, event 601; part 3: the dot field, the caption and glyph 3 with the per-frame level, event 301. The random generator replayed (`cmc::Random`) | `notes-particles.md` (7 the time bases and the capture's frame steps, 8 the shortcuts) | core |

The engine, grid and particles jobs can run in parallel; parts3d starts when the engine's API is in place.

## Quality

- Smooth at 60+ fps at 2560 × 1920 on a mid-range GPU; all initialisation of all parts together under 2 s (the exe,
  the soundtrack and the pictures take about 0.2 s now, the five smooth textures 60 ms).
- Anti-aliased edges (analytic coverage in shaders, or MSAA for geometry).
- Portable C++20 (g++, MinGW, MSVC), no new dependencies. Every file starts with a comment saying what it draws or
  provides and which original routines (by address) it ports. Small readable functions.
- Write down in `clone/remake/README.md` ("Known differences") what cannot be exact, and in the scene file what was
  evaluated exactly instead of the original's shortcut.

## Your final message

Under 300 words: what you built (files, public API), how you verified it (compare sheets, numbers), what differs
from the original and why, and what is left.
