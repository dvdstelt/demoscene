# Paper remake — shared brief for everyone writing code in paper/remake

"Paper" (Psychic Link: code Statix, music Vic of Acme; Wired '96 64k intro, MS-DOS) is remade in C++20 / SDL3 /
OpenGL 3.3 core. Goal, in the user's words: **the same as the original, but way sharper**. Same motion, timing,
composition, colours and music as the original; rendered at any resolution in true colour.

The original has been fully reverse engineered; read the notes before writing code:
- paper/re/NOTES.md, PARTS.md (part timeline), REMAKE_PLAN.md
- paper/re/notes-*.md: init (shared code, tables, objects, palette system),
  title, sketch, scene3d, logo, planes, water (greetings), music. Every routine is given as pseudo-C with exact
  constants and was verified against the emulator. The decompile agents' Python models and verification scripts
  are in the scratch directories named at the end of each notes file
  (paper/re/models/<scope>/).
  They are a good source for exact reference values.

## Fidelity rules
- Keep: timing (to the tick), motion paths, camera, composition, colours, the random realisations (same RNG
  sequence, so the same flock, jitter and strokes as the capture), and visible quirks of the original that are part
  of its look (each notes file lists them).
- Evaluate exactly instead of copying low-resolution shortcuts: 8x8-block interpolation -> per pixel; lookup tables
  (sine, reciprocal, shade ramps of 32 steps, integer fade steps) -> continuous float math; repeated box blurs ->
  Gaussians of the same variance; 1-2 px lines -> resolution-scaled strokes; nearest-neighbour texture reads ->
  filtered (bicubic where it helps); integer pixel polygon vertices -> float with antialiased edges.
- Drop artefacts that only exist because of 320x200 / 8 bit (stale rows, truncation steps, aliasing), but never
  change what the viewer would describe as the content.
- Nothing from PAPER.EXE may be copied into the source: read every static table, image, string, object and
  constant table from the exe at run time through `PaperExe` (src/exe.h) by its code32 address. Format constants
  that are code (multipliers, loop counts) may appear in the source as the code does.

## Layout
- `src/main.cpp` program, options, playback loop, `--shots` / `--video` / `--wav` (coordinator)
- `src/exe.*` PAPER.EXE access (`PaperExe`: `u8/u16/u32/s32/f32/bytes(addr)` by code32 offset); `src/pklite.cpp`
- `src/music.*` the player + GF1 model; the clock (`Music::tick()`); `counter(t)` = music counter [0x1ea8b]
- `src/gl.*`, `src/render.*` GL loader and helpers (Shader, Texture, Target, Mat4, Vec3; `kCommonGLSL` is prepended
  to every fragment shader: hash12, rot2, texBicubic, luma)
- `src/demo.*` framework: `Scene`, `REGISTER_SCENE`, `Ctx`, `TickStepper`, target pool, timeline (`timeline.cpp`)
- `src/paper/` the core library (shared by several parts): `core2d.h` maps the 2D part (RNG positions, palettes,
  fade post pass, font, textures, oscillators, warp, blur, page peel); math3d.h / obj3d.h / render3d.h / pencil.h
  are the 3D part (exact 16.16 matrices, all objects from the exe, Renderer3D = 0x3fb3 + 0x43df on the GPU with
  MSAA, PencilInk = the pencil lines and the paper darkening resolved through the palette ramps).
  src/scenes/coretest.cpp and core3dtest.cpp show how the core is used.
  Part agents do not edit core files (other agents build on them at the same time): work around a problem in your
  own files and report it.
- `src/scenes/<part>.cpp` one file (or a few) per part agent
- `tools/compare.py` side-by-side check against the capture

## Building and running (inside the toolbox container "acme303": g++, cmake, ninja, SDL3-devel, python3 + numpy/PIL)
Use your own build directory, and build only your own part files:
```
cd paper/remake
toolbox run -c acme303 cmake -S . -B build-<you> -G Ninja -DPAPER_PARTS="title"
toolbox run -c acme303 cmake --build build-<you>
toolbox run -c acme303 ./build-<you>/paper --shots 500,700 --shotdir <scratch>
toolbox run -c acme303 python3 tools/compare.py <scratch>/cmp.jpg --shots <scratch> 500 700 [--crop x,y,w,h] [--score]
```
(then look at cmp.jpg with the Read tool). `PAPER_PARTS` lists basenames in src/scenes; parts not built show a flat
placeholder colour. The program finds ../PAPER.EXE by itself (the PKLITE unpacker and the music player are done).
The core library (src/paper/*.cpp) is always built.
`--video out.mp4 --from T0 --to T1` renders a tick range at 70 fps through the host ffmpeg (with the music);
`--bench T0,T1 --shotsize 2560x1920` reports the GPU time per frame over a tick range;
`--window --tick T --info` plays from tick T with the tick and part in the window title.
Other people build in this tree at the same time: if the build breaks in a file that is not yours, wait a minute and
retry; never edit someone else's file to fix it — report it in your final message instead.

## Conventions
- **Time** is the original's IRQ0 tick (70.0018 Hz) from program start, as in PARTS.md and full.frames; fractional.
  A part's `render(ctx, tick)` must depend only on `tick` (players seek; shots render ticks in any order).
  Per-tick state q_n (the value after the IRQ of tick n) is stepped exactly as the original (use `TickStepper`,
  which replays from the part start after a backwards seek; keep stepping cheap). At a fractional time τ, show
  q_⌊τ⌋ interpolated towards q_⌊τ⌋+1 by frac(τ) for continuous quantities (positions, angles, fades, peel), and
  q_⌊τ⌋ for discrete ones. So at an integer tick the remake shows the state after that tick's IRQ.
- **Reference capture** `paper/re/full.frames`: record t = the last whole frame the original finished before the IRQ
  of tick t+1 (64000 bytes VGA + 768 bytes 6-bit DAC palette, i.e. with the fade level applied). A frame finished
  late in tick t was mostly drawn with the state after IRQ t, sometimes t−1 (the original drew ~1.4 frames per
  tick, the flock ~0.5). Expect at most one tick of offset; check alignment on a moving detail.
  `paper/re/full.ticks`: per tick: tick, music counter, effect callback, main EIP, frames drawn so far.
  `paper/paper_reference.mp4` is the same capture as a 70 fps video with the music.
- **Screen**: the original's 320x200 screen is shown at 4:3. `ctx.out` is a 4:3 RGBA16F target of any size. "VGA
  coordinates": x 0..320 left to right, y 0..200 top to bottom over the whole frame; `ctx.vgaProj()`,
  `ctx.drawTexture(...)`, `ctx.rect(...)` work in them. Pixel centres of the original are at (x+0.5, y+0.5).
  A 3D projection of the original (centre (160,100), focal length f in pixels) maps to clip space as
  x_clip = (x_vga/160 − 1), y_clip = (1 − y_vga/100).
- **Colour**: display-space floats 0..1 (no sRGB conversion); a 6-bit palette value v is v/63.
- **The global palette fade level** [0x1edb5] is applied once to the finished frame by the core library's post pass
  (it is a per-colour function, so applying it to the final RGB frame equals the original's DAC upload). Parts draw
  with the *working* palette (level 0x8000) and must not apply the level themselves.
- **Shared objects**: `ctx.shared<T>()` gives one instance of T per context (constructed as T(ctx) on first use) —
  use it for tables, textures and shaders that are expensive or shared. Frame-size targets: borrow with
  `ctx.temp()` (released when the lease goes out of scope); the frame size changes when the window is resized.
- **Other parts**: `ctx.renderPart("logo", 6017, target)` renders another part at a tick into a target (used for
  transitions such as the page peel).
- **Quality**: smooth at 60+ fps at 2560x1920 on a mid-range GPU; all init of all parts together < 2 s. Antialias
  edges (analytic coverage in shaders, or MSAA / supersampling for geometry).
- **Code**: portable C++20 (g++, MinGW, MSVC), no new dependencies, every file starts with a comment saying what it
  draws or provides and which original routines (code32 addresses) it ports. Small readable functions.

## Your final message (all agents)
Under 300 words: what you built (files, public API), how you verified it (compare sheets / numbers), what differs
from the original and why, and what is left.
