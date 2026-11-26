# Vivid Experiment remake: shared brief for everyone writing code in `vividexp/remake`

*Vivid Experiment* (Doomsday, Assembly '96 PC demo, 3rd place; code Doom and MRI, graphics Dice and Damac, music Wode
and Swallow; MIDAS Sound System 0.5a by Sahara Surfers) is remade in C++20 / SDL3 / OpenGL 3.3 core. The goal: **the
same as the original, but much sharper**. Same motion, timing, composition, colours and music as the original,
rendered at any resolution in true colour.

The original has been reverse engineered. Read the notes before writing code:

- `vividexp/re/NOTES.md` (unpacking, the archives, the clock, the script), `PARTS.md` (the timeline with refreshes,
  music positions and script lines);
- `vividexp/re/notes-*.md`: `core` (script commands, pictures, palette fades, the zoom credits, the clock),
  `music`, `bitmap`, `bump`, `water`, `generic`. Every routine is pseudo-C with exact constants, checked against the
  emulator; the Python models in `vividexp/re/models/<scope>/` give exact reference values;
- `AGENTS.md` in the repository root (the rules).

## Fidelity rules

- Keep: timing, motion paths, camera, composition, colours, and the visible quirks that are part of the look (each
  notes file lists them and says which ones a remake may drop).
- Evaluate exactly instead of copying low-resolution shortcuts: per-pixel tables that follow a formula become the
  formula; affine texture mapping becomes perspective-correct where the notes say the original meant perspective;
  nearest-neighbour texel reads become filtered reads **in colour** (look each tap's index up in the palette, then
  blend the colours: never filter indices); integer positions and truncated normals become floats; polygon edges are
  anti-aliased.
- **Keep the original's grain.** Noise, dither and texture patterns are drawn with one cell per original pixel, not
  finer (the maintainer had another remake's noise changed back to the original's coarseness: finer snow looked like
  a different texture). Smooth what is smooth in the original (gradients, light, motion); keep what is grainy.
- Drop artefacts that only exist because of 320 × 200 / 8 bit (truncation steps, 6-bit palette steps, aliasing,
  tearing, a frame of the previous part shown in the next part's palette), but never change what a viewer would
  describe as the content.
- Nothing from the originals may be copied into the source: read every table, picture, palette, mesh, script and
  parameter at run time: `c.exe->u8/u16/s16/u32/s32/f32/f64/cstr/bytes/span(addr)` (`src/exe.h`, linear addresses as
  in the notes) and `c.exe->file(name)` for the archives' files. **Read the file names from the script lines or
  the exe** (the strings the routines use, by address), so the source holds addresses rather than the demo's
  strings. Code constants (multipliers, loop counts, addresses, offsets, format constants) may appear in the source,
  and so may values measured from the capture.

## Time

**The clock is the screen refresh of the original.** `render(c, r)` gets `r` = a fractional refresh number counted as
in the capture: refresh k is record k of `vividexp/re/full.frames`. Part I's module starts at refresh 23
(`c.music->song[0].startRefresh`); playback, `--start`, `--shots` and `--video` work in seconds of music time and
convert (`c.music->refreshOf(t)`); `--refshots R1,R2` renders refreshes directly, for comparing with the capture.

The original's clock is the frame counter [0x825c5c], +1 per refresh in MIDAS's timer interrupt; the effects
animate from it and the palette fades step by it. The script (`src/vivid/timeline.*`) runs each line when the music
reaches its position, as an infinitely fast machine would (no load times):

| what | in the remake |
|---|---|
| the segment being drawn | `c.seg`: `scene`, `part` (1, 2), `start`, `end` (refreshes), `run` (index of its Run line in `c.timeline->lines()`), `init` (the Init line that set the effect up) |
| a script line | `c.timeline->line(i)`: `line.command()`, `line.arg(k)`, `argInt(k)`, `argFloat(k)`, `line.pos`, `line.pat`; `start` / `end` refreshes |
| refreshes since the Run command started | `c.since(r)` = r − `c.seg->start` |
| state that outlives a Run (several Runs of one Init, or effects that never reset) | look the other lines up in `c.timeline->lines()` and replay from the first one (the notes say what persists) |
| the music position | `c.music->song[part - 1].positionAt(r)` |
| the capture | record `c.captureRecord(r)` |

**Stepped state.** Where the original steps state once per refresh, replay it from its start on every frame with a
closed form or `TickStepper`, and blend continuous quantities between step n and n + 1 by the fraction, so motion is
smooth at any refresh rate. Sudden changes stay on their step.

**Seeking.** `render(c, r)` must depend only on `r`: players seek, and shots and videos render refreshes in any
order. Check: the frame at a refresh must be the same after seeking forwards, after seeking backwards and while
playing.

## Screen and colour

- The original's 320 × 200 8-bit screen is shown at 4:3. `c.out` is a 4:3 target of any size (bound, cleared to
  black). "VGA coordinates": x 0..320, y 0..200, top-left origin; the original pixel (x, y) covers [x, x+1] ×
  [y, y+1], its centre is (x + 0.5, y + 0.5); one original pixel is 1.2 times as tall as wide on screen.
  `c.vgaProj()`, `c.drawTexture(...)`, `c.rect(...)` work in them.
- **Colour = the palette entry of the pixel's index**, as a display-space float: a 6-bit value v is v / 63 (no sRGB
  conversion). **The palette is the DAC's at refresh r, fades included**: `c.paletteTex()` (256 × 1 RGBA32F, bound
  to whatever unit you choose with `bind(unit)`), `c.palette(r)` on the CPU. Draw your indices through it, and the
  fades and flashes of the script come out right by themselves.
- `src/vivid/images.h`: `vivid::loadPicture(exe, name)` (PCX and RAW as the original decodes them, with their own
  palettes), `vivid::loadPal`, `vivid::uploadIndices` (GL_R8UI, read with `texelFetch` from a `usampler2D`),
  GLSL `vivid::kIndexedGLSL` (`palColor(pal, index)`, `idxAt(tex, texel)` with wrapping, `idxClamp`). Filter in
  colour: fetch the four texels around the sample point (texel n covers [n, n+1), so sample at uv − 0.5), look each
  up, blend.

## Pictures

`vividexp/remake/art/<name>.png` will hold the redraws of the pictures drawn as they are (a separate job; the notes
say which pictures qualify: never photos of real people; anything read through palette indices or tables keeps the
original's indices). `c.pictures->indexed(file)` / `indices(file)` give the original; `c.pictures->redraw(file)`
the redraw or nullptr. A scene that draws a picture as it is must work with both.

## Layout

- `src/main.cpp` program and options; `src/demo.*` framework (`Scene`, `REGISTER_SCENE`, `Ctx`, `TickStepper`, the
  target pool, the placeholder); `src/render.*`, `src/gl.*` GL helpers (`Shader`, `Texture`, `Target`, `Mat4`,
  `Vec3`; `kCommonGLSL` is prepended to every fragment shader); `src/exe.*` + `src/unpack.cpp` VIVID.EXE and its
  archives; `src/pictures.*` the pictures and the art switch; `src/player.*` playback; `src/music.*` (and the player
  port) the soundtrack.
- `src/vivid/` the core library: `script.*`, `timeline.*` (the running order, the palette), `images.*`.
- `src/scenes/<scene>.cpp` one file per scene: `pictures`, `zoom`, `bitmap`, `bump`, `water`, `generic`, each with
  `REGISTER_SCENE(MyScene, "<scene>")`.
- `tools/compare.py` side-by-side check against the capture.

Scene agents do not edit core files (`src/*.cpp`, `src/*.h`, `src/vivid/*`, `CMakeLists.txt`; others build on them
at the same time): work around a problem in your own files and report it. Your own helpers go in your scene file or
in `src/scenes/<scene>_*.cpp|h`.

## Building and checking (inside the toolbox container "acme303")

Use your own build directory, and build only your own scene files:

```
cd vividexp/remake
toolbox run -c acme303 cmake -S . -B build-<you> -G Ninja -DVIVID_PARTS="bump"
toolbox run -c acme303 cmake --build build-<you>
toolbox run -c acme303 ./build-<you>/vividexp-remake --cues                 # the segments and their refreshes
toolbox run -c acme303 ./build-<you>/vividexp-remake --refshots 17300,17800.5 --shotdir <scratch> --shotsize 960x720
toolbox run -c acme303 python3 tools/compare.py <scratch>/cmp.jpg --shots <scratch> 17300 17800.5 [--crop x,y,w,h] [--score]
```

Then look at `cmp.jpg` with the Read tool: left the capture's record, right the remake at that refresh. Look at
zoomed crops too (`--crop`). `VIVID_PARTS` lists basenames in `src/scenes`; scenes that are not built show a
placeholder. `--video out.mp4 --from T0 --to T1` renders a range in seconds of music time at 60 fps; `--bench
T0,T1 --shotsize 2560x1920` reports the GPU time per frame; `--window --start T --info` plays from T with the
position, scene and refresh in the title. Delete your build directory when you are done.

**The soundtrack is a placeholder until the MIDAS port lands**: its row timing comes from the modules' speed and
tempo alone, so segment starts can be off by up to ~60 refreshes from the capture's late in the demo. Compare
within a segment by refreshes since its start (`--cues` gives the remake's start, `vividexp/re/full.script` the
capture's), or ask the coordinator for the port's state.

The capture shows the original at 100 M instructions per second: compare composition, motion, colours and timing,
not single pixels. The snapshots in `vividexp/re/snaps/` (whole demo) and `snaps2/` (part II alone; record k of
`part2.frames` = record k + 14,444 of `full.frames`) and the models in `vividexp/re/models/` give exact intermediate
values.

## Quality

- Smooth at 60+ fps at 2560 × 1920 on a mid-range GPU; all initialisation of all scenes together under 2 s.
- Anti-aliased edges (MSAA for geometry, or analytic coverage in shaders).
- Portable C++20 (g++, MinGW, MSVC), no new dependencies, no warnings. Every file starts with a comment saying what
  it draws or provides and which original routines (by address) it ports. Small readable functions.
- Write down in your final message what cannot be exact (it goes into `vividexp/remake/README.md`, "Known
  differences"), and in the scene file what was evaluated exactly instead of the original's shortcut.

## Your final message

Under 300 words: what you built (files, public API), how you verified it (compare sheets, numbers), what differs
from the original and why, and what is left.
