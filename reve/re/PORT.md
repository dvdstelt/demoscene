# Reve remake — shared brief for everyone writing code in `reve/remake`

*Reve* (Pulse, The Party 1995 PC demo, final version 1996; code LCA, graphics Antony and AJT, music Shad) is remade in
C++20 / SDL3 / OpenGL 3.3 core. The goal: **the same as the original, but much sharper**. Same motion, timing,
composition, colours and music as the original, rendered at any resolution in true colour.

The original has been reverse engineered. Read the notes before writing code:

- `reve/re/NOTES.md` (the files, the extender, the program flow, the script table, the clock),
  `PARTS.md` (the timeline: script entries, refreshes, music positions, frame rates, what is on screen);
- `reve/re/notes-*.md`: one per scope; every routine is pseudo-C with exact constants, checked against the emulator;
  the Python models in `reve/re/models/<scope>/` give exact reference values;
- `AGENTS.md` in the repository root (the rules).

## Fidelity rules

- Keep: timing, motion paths, camera, composition, colours, random sequences (replay the original's generator from
  its seed), and the visible quirks that are part of the look (each notes file lists them and says which ones a
  remake may drop).
- Evaluate exactly instead of copying low-resolution shortcuts: lookup tables become formulas; affine texture mapping
  becomes perspective-correct where the original means a perspective view; nearest-neighbour texel reads become
  filtered reads **in colour** (look each tap's index up, then blend the colours: never filter indices); shade-table
  levels become a continuous level (blend the table's two neighbouring levels in colour); integer positions become
  floats; the painter's order becomes a depth buffer where nothing visible depends on the order; polygon edges are
  anti-aliased (MSAA or analytic coverage); full-screen table blurs become the blur they approximate, in colour.
- Drop artefacts that only exist because of 320 × 200 / 8 bit (truncation steps, 6-bit palette steps, aliasing,
  seams of the fixed point), but never change what a viewer would describe as the content.
- Nothing from the original may be copied into the source: read every table, picture, palette, mesh and parameter at
  run time: `c.exe->u8/u16/s16/u32/s32/f32/f64/cstr/bytes/span(addr)` (`src/exe.h`, addresses as in the notes: the
  image at 0x20427c) and `c.exe->file(name)` for the archive's files. **Read the file names from the image too** (the
  strings the routines pass to the loader, by address), so the source holds addresses rather than the demo's
  strings. Constants that a routine has as immediates in its code may be read from the code bytes by address. Code
  constants (multipliers, loop counts, addresses, offsets, format constants) may appear in the source, and so may
  values measured from the capture.

## Time

**The clock is the screen refresh of the original.** `render(c, r)` gets `r` = a fractional refresh number counted as
in the capture: refresh k is record k of `reve/re/full.frames` (the screen at the start of refresh k's vertical
retrace). Playback, `--start`, `--shots` and `--video` work in seconds of music time and convert
(`music.refreshOf(t)`); `--refshots R1,R2` renders refreshes directly, for comparing with the capture.

| clock | in the remake |
|---|---|
| the retrace routine's calls (the timer interrupt calls the script entry's retrace routine once per refresh, at the retrace, from the entry's first refresh on) | `c.refreshes(r)` = r − `c.start`: record r shows the state after n = r − c.start calls; evaluate it continuously at that n |
| what a **frame routine** advances per drawn frame | frame-rate dependent: the notes give the capture's frame rate; the 2D parts draw every refresh in the capture, so step them once per refresh, as continuous motion |
| the music position order << 8 \| row | `c.position(r)`; `c.music->refreshOfPosition(pos)` = the first refresh whose retrace sees it |
| the script table 0x205ed0 (read from the image) | `c.script()`: each entry's routines, end position and its `start` / `end` refreshes; `c.cue->entry` is the entry being drawn |
| the capture | `c.captureRecord(r)` = the `full.frames` record shown at r |

**Entry starts** (`src/timeline.cpp`, `reve-remake --cues`): one cue per script entry. Its `c.start` is the
capture's own first retrace call S of the entry ("retrace routine set at frame S" in `full.log`): the original's
init runs 1 to 12 refreshes after the music reaches the previous entry's end position P (it unpacks files), and
the remake takes the capture's time for it, so **record r shows the state after r − S retrace calls**, in every entry
(the decompiling agents verified this rule). Between P + 1 and S the framework shows the previous entry's frame of
refresh P + 1 (`Cue::hold`), or black where the next init clears the screen first (`Cue::black`); a part never
needs to handle that gap. `c.end` is the next entry's S.

**Stepped state.** Replay what the original steps per refresh from the entry start on every frame with closed forms
or `TickStepper`, and blend continuous quantities between step n and n + 1 by the fraction, so motion is smooth at
any refresh rate. Sudden changes stay on their step.

**Seeking.** `render(c, r)` must depend only on `r`: players seek, and shots and videos render refreshes in any
order. Check: the frame at a refresh must be the same after seeking forwards, after seeking backwards and while
playing.

## Screen and colour

- The original's 320 × 200 8-bit screen is shown at 4:3. `c.out` is a 4:3 RGBA16 target of any size (bound, cleared
  to black). "VGA coordinates": x 0..320, y 0..200, top-left origin; the original pixel (x, y) covers [x, x+1] ×
  [y, y+1], its centre is (x + 0.5, y + 0.5); one original pixel is 1.2 times as tall as wide on screen.
  `c.vgaProj()`, `c.drawTexture(...)`, `c.rect(...)` work in them.
- **Colour = the palette entry of the pixel's index**, as a display-space float: a 6-bit value v is v / 63 (no sRGB
  conversion). The RAW files carry 8-bit palettes; the demo's loader gives the DAC v >> 2 (`RawImage::pal6`).
- Palette effects the original runs (fades, flashes) are the part's job: do them in colour.

## Pictures

`reve/remake/art/<name>.png` will hold the redraws of the pictures drawn as they are (a separate job; the notes say
which pictures qualify; anything read through palette indices or tables keeps the original's indices).
`c.picture(name)` gives a `Picture` (`src/pictures.h`: the original's pixels through their palette, or the redraw),
`c.drawPicture(p, PictureDraw{...})` draws it. `rawImage(exe, file)` reads a RAW file's indices and palette for
parts that work with indices. Register a picture you draw as it is in `pictureList()` (`src/pictures.cpp`) through
the coordinator.

## Layout

- `src/main.cpp` program and options; `src/timeline.cpp` the running order; `src/demo.*` framework (`Scene`,
  `REGISTER_SCENE`, `Ctx`, `TickStepper`, the target pool, the placeholder); `src/render.*`, `src/gl.*` GL helpers
  (`Shader`, `Texture`, `Target`, `Mat4`, `Vec3`; `kCommonGLSL` is prepended to every fragment shader); `src/exe.*` +
  `src/unpack.cpp` REVE.EXE; `src/pictures.*` the pictures and the art switch; `src/player.*` playback;
  `src/music.*`, `src/lcaplay.*`, `src/gf1.*` the soundtrack.
- `src/reve/` the core library shared by the parts (the 3D engine).
- `src/scenes/<scene>.cpp` one file per part, named as the scenes of timeline.cpp, each with
  `REGISTER_SCENE(MyScene, "<scene>")`.
- `tools/compare.py` side-by-side check against the capture.

Part agents do not edit core files (`src/*.cpp`, `src/*.h`, `CMakeLists.txt`; others build on them at the same
time): work around a problem in your own files and report it.

## Building and checking (inside the toolbox container "acme303")

Use your own build directory, and build only your own part files:

```
cd reve/remake
toolbox run -c acme303 cmake -S . -B build-<you> -G Ninja -DREVE_PARTS="title"
toolbox run -c acme303 cmake --build build-<you>
toolbox run -c acme303 ./build-<you>/reve-remake --refshots 300,450.5 --shotdir <scratch> --shotsize 960x720
toolbox run -c acme303 python3 tools/compare.py <scratch>/cmp.jpg --shots <scratch> 300 450.5 [--crop x,y,w,h] [--score]
```

Then look at `cmp.jpg` with the Read tool: left the capture's record, right the remake at that refresh. `REVE_PARTS`
lists basenames in `src/scenes`; parts that are not built show a placeholder. `--video out.mp4 --from T0 --to T1`
renders a range in seconds of music time at 60 fps; `--bench T0,T1 --shotsize 2560x1920` reports the GPU time per
frame; `--window --start T --info` plays from T with the position, part and refresh in the title. Delete your build
directory when you are done.

The capture shows the original at 100 M instructions per second: compare composition, motion, colours and timing,
not single pixels (the remake is sharper by design).
