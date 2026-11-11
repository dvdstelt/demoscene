# JIZZ remake — shared brief for everyone writing code in `jizz/remake`

*JIZZ* (The Black Lotus, Wired '97 64k intro, MS-DOS; code and design Jace, music Probe, vector engine and blobs
Nix) is remade in C++20 / SDL3 / OpenGL 3.3 core. The goal: **the same as the original, but much sharper**. Same
motion, timing, composition, colours and music as the original, rendered at any resolution in true colour.

The original has been reverse engineered. Read the notes before writing code:

- `jizz/re/NOTES.md` (DOS32, VESA, MXMPlay, the timer, program flow, the sync table), `PARTS.md` (part timeline);
- `jizz/re/notes-*.md`: `core` (buffers, tables, random generator, frame loop, colour tables, flash, fades, glow
  lines), `textures` (the texture generator and its scripts), `engine` (Nix's vector engine), `music` (decruncher,
  MXM, MXMPlay, GF1), `parts2d` (TBL, the jizz logo, radial zoom, field lines, end picture), `parts3d-a` (ribbons,
  tunnel), `parts3d-b` (tube, spiky object, metaballs). Every routine is pseudo-C with exact constants, checked
  against the emulator. The Python models the decompiling agents wrote are in `jizz/re/models/<scope>/` and are a
  good source of exact reference values.
- `AGENTS.md` in the repository root: the rules of this repository.

## Fidelity rules

- Keep: timing, motion paths, camera, composition, colours, random sequences (replay the original's generator
  0x141f6 from its seed, so the jitter is the capture's), and the visible quirks that are part of the look (each
  notes file lists them).
- Evaluate exactly instead of copying low-resolution shortcuts: lookup tables (polar table, sine, 1/sqrt, spot
  table, colour tables, fade tables) become continuous float maths; 5/6-bit colour becomes float; integer DDA lines
  become anti-aliased strokes of the same width relative to the screen (4/320 of the width for the 4-pixel lines);
  2x2 box blurs become Gaussians of the same variance at the output resolution; nearest-neighbour texture reads
  become filtered reads (bilinear or bicubic, wrapping like the original's 256x256 maps); polygon edges are
  anti-aliased (MSAA or analytic coverage).
- Drop artefacts that only exist because of 320x200 / 16 bit (truncation steps, aliasing, missing corner pixels),
  but never change what a viewer would describe as the content.
- Nothing from JIZZ.EXE may be copied into the source: read every static table, picture, texture script, object
  parameter set and string from the exe at run time through `JizzExe` (`src/exe.h`: `u8/u16/u32/s32/f32/bytes(addr)`,
  addresses as in the notes). Constants that are code (multipliers, loop counts, addresses) may appear in the source.

## Time

- **Clock**: `render(ctx, t)` gets `t` = seconds of music time (0 = xmpPlay starts the music; capture tick 434).
  `ctx.timer(t)` is the original's xmpGetTimer at t (PIT clocks, 1,193,182 per second, continuous). Where the
  original computes from the timer, evaluate the same formula with the continuous timer (no tick quantisation).
  `ctx.position(t)` is xmpGetPos (order << 8 | row), `ctx.positionTime(pos)` the music time of a position,
  `ctx.event(t, &since)` the sync event [0x6413] as 0x638b sets it (and when it was set). Parts that rewrite the
  event themselves model that in their own code.
- **Part starts**: the original enters a part at the first frame after the music reaches its position (up to two
  frames late) and reads its start timer value there. The remake starts every part exactly at its position's
  music time; take the part's own start timer value as `ctx.timer(start)`.
- **Per-frame state**: what the original advances once per drawn frame (TBL fades, colour pulse and feedback, the
  03:10 inversion decay, the rings of 0x18009, the glow trail and line decay, the rectangle jitter, the field-line
  speed factor, ...) runs at a fixed rate: the frame rate the capture shows in that phase (measure it in
  `jizz/re/full.flips`; the notes give most of them, e.g. TBL 70/3 fps, 0x17003 35 fps). Step it exactly as the
  original with `TickStepper` (`steps = (t - phaseStart) * rate`) and blend continuous quantities between step n and
  n+1 by the fraction, so motion is smooth at any refresh rate. Sudden changes stay on their step.
- **Seeking**: `render(ctx, t)` must depend only on `t`: players seek, and shots and videos render times in any
  order. Cheap per-frame state is replayed from the phase start on every frame. Feedback buffers on the GPU (TBL's
  trail, the glow trail) are kept between frames and advanced step by step while playing forwards; after any other
  jump they are rebuilt by simulating the last N steps from a cleared buffer, with N large enough that what came
  before contributes less than 1/1000 (e.g. 7/8 per step: 52 steps). Check: the frame at a time must be the same
  after seeking forwards, after seeking backwards and while playing.

## Screen and colour

- The original's 320x200 screen is shown at 4:3. `ctx.out` is a 4:3 RGBA16F target of any size. "VGA coordinates":
  x 0..320, y 0..200, top-left origin; pixel centres of the original are at (x + 0.5, y + 0.5). `ctx.vgaProj()`,
  `ctx.drawTexture(...)`, `ctx.rect(...)` work in them. A projection with centre (cx, cy) and focal length f in
  pixels maps to clip space as x_clip = x_vga / 160 - 1, y_clip = 1 - y_vga / 100.
- Colours are display-space floats 0..1 (no sRGB conversion). An 8-bit channel v is v / 255; a 5:6:5 pixel is
  (r / 31, g / 63, b / 31).
- The timer fades (0x17979 / 0x17999: from white, or black in 0x1212a, to the picture) and the engine's colour
  tables (E, S and the inversion F of the flash) are provided by the core library; parts call them, they are not a
  global post pass.

## Layout

- `src/main.cpp` program, options, playback, `--shots` / `--video` / `--wav` / `--bench`; `src/timeline.cpp` the
  running order; `src/demo.*` framework (`Scene`, `REGISTER_SCENE`, `Ctx`, `TickStepper`, target pool);
  `src/render.*`, `src/gl.*` GL helpers (`Shader`, `Texture`, `Target`, `Mat4`, `Vec3`; `kCommonGLSL` is prepended
  to every fragment shader: hash12, rot2, texBicubic, luma); `src/exe.*` + `src/adam.cpp` JIZZ.EXE;
  `src/music.*` + `src/mxmplay.*` the soundtrack and the clock.
- `src/jizz/` the core library shared by several parts: tables and the random generator, the texture generator,
  colour tables / inversion, fades, glow lines, polar mapping, the vector engine. Its headers say what each part can
  use.
- `src/scenes/<part>.cpp` one file (or a few) per part: `tbl`, `ribbons`, `tunnel`, `tube`, `logo`, `spiky`,
  `zoom`, `field`, `blobs`, `end` (the names in `src/timeline.cpp`).
- `tools/compare.py` side-by-side check against the capture.

Part agents do not edit core files (others build on them at the same time): work around a problem in your own files
and report it. Core agents do not edit scene files.

## Building and checking (inside the toolbox container "acme303")

Use your own build directory, and build only your own part files:

```
cd jizz/remake
toolbox run -c acme303 cmake -S . -B build-<you> -G Ninja -DJIZZ_PARTS="tbl;logo"
toolbox run -c acme303 cmake --build build-<you>
toolbox run -c acme303 ./build-<you>/jizz-remake --shots 2.5,7.25 --shotdir <scratch> --shotsize 960x720
toolbox run -c acme303 python3 tools/compare.py <scratch>/cmp.jpg --shots <scratch> 2.5 7.25 [--crop x,y,w,h] [--score]
```

Then look at `cmp.jpg` with the Read tool. `JIZZ_PARTS` lists basenames in `src/scenes`; parts that are not built
show a flat placeholder colour. The program finds `jizz/original/JIZZ.EXE` by itself. `--video out.mp4 --from T0 --to
T1` renders a range at 60 fps through the host ffmpeg (with the music); `--bench T0,T1 --shotsize 2560x1920` reports
the GPU time per frame; `--window --start T --info` plays from T with the time and part in the window title.
Other people build in this tree at the same time: if the build breaks in a file that is not yours, wait a minute and
retry; never edit someone else's file to fix it — report it in your final message instead. Delete your build
directory when you are done.

The capture (`jizz/re/full.frames`, see `jizz/re/BRIEF.md`) shows the original at 320x200 with its frame rate:
compare composition, motion, colours and timing, not single pixels. Expect differences of up to two frames at part
starts (the original's frame-locked part changes).

## Quality

- Smooth at 60+ fps at 2560x1920 on a mid-range GPU; all initialisation of all parts together under 2 s.
- Anti-aliased edges (analytic coverage in shaders, or MSAA for geometry).
- Portable C++20 (g++, MinGW, MSVC), no new dependencies. Every file starts with a comment saying what it draws or
  provides and which original routines (by address) it ports. Small readable functions.

## Your final message

Under 300 words: what you built (files, public API), how you verified it (compare sheets, numbers), what differs
from the original and why, and what is left.
